    ThreadPool& eval_pool = []() -> ThreadPool& {
        static ThreadPool pool((int)std::max(1u, thread_budget::max_threads(8)));
        return pool;
    }();

    ClassificationResult out;

    const int nx = (int)(pad_w / 8);
    const int ny = (int)(pad_h / 8);
    const int nb = nx * ny;
    out.block_types.assign((size_t)nb, FileHeader::BlockType::DCT);

    const CopyParams kLosslessCopyCandidates[4] = {
        CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
    };

    const CopyParams kTileMatch4Candidates[16] = {
        CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
        CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
        CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
        CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
    };

    struct LosslessModeParams {
        int palette_max_colors = 2;
        int palette_transition_limit = 63;
        int64_t palette_variance_limit = 1040384;
    } mode_params;

    if (profile_id == 0) { // UI
        mode_params.palette_max_colors = 8;
        mode_params.palette_transition_limit = 58;
        mode_params.palette_variance_limit = 2621440;
    } else if (profile_id == 1) { // ANIME
        mode_params.palette_max_colors = 8;
        mode_params.palette_transition_limit = 62;
        mode_params.palette_variance_limit = 4194304;
    }

    auto evaluate_block = [&](int i) -> BlockEval {
        BlockEval ev;
        int bx = i % nx;
        int by = i / nx;
        int cur_x = bx * 8;
        int cur_y = by * 8;

        int64_t sum = 0;
        int64_t sum_sq = 0;
        int16_t unique_vals[9];
        int unique_cnt = 0;
        bool has_prev = false;
        int16_t prev_v = 0;
        for (int y = 0; y < 8; y++) {
            const int16_t* src_row =
                &padded[(size_t)(cur_y + y) * (size_t)pad_w + (size_t)cur_x];
            const int row_base = y * 8;
            for (int x = 0; x < 8; x++) {
                const int idx = row_base + x;
                const int16_t v = src_row[x];
                ev.block[(size_t)idx] = v;
                sum += v;
                sum_sq += (int64_t)v * (int64_t)v;
                if (has_prev && prev_v != v) ev.transitions++;
                prev_v = v;
                has_prev = true;

                bool found = false;
                for (int u = 0; u < unique_cnt; u++) {
                    if (unique_vals[u] == v) {
                        found = true;
                        break;
                    }
                }
                if (!found && unique_cnt < 9) {
                    unique_vals[unique_cnt++] = v;
                }
            }
        }
        ev.unique_cnt = unique_cnt;

        ev.variance_proxy = sum_sq - ((sum * sum) / 64);

        ev.palette_transitions = ev.transitions;

        if (i > 0) {
            for (const auto& cand : kLosslessCopyCandidates) {
                int src_x = cur_x + cand.dx;
                int src_y = cur_y + cand.dy;
                if (src_x < 0 || src_y < 0) continue;
                if (src_x + 7 >= (int)pad_w || src_y + 7 >= (int)pad_h) continue;
                if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                bool match = true;
                for (int y = 0; y < 8; y++) {
                    const int16_t* dst_row =
                        &padded[(size_t)(cur_y + y) * (size_t)pad_w + (size_t)cur_x];
                    const int16_t* src_row =
                        &padded[(size_t)(src_y + y) * (size_t)pad_w + (size_t)src_x];
                    if (std::memcmp(dst_row, src_row, 8 * sizeof(int16_t)) != 0) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    ev.copy_found = true;
                    ev.copy_candidate = cand;
                    break;
                }
            }
        }
        if (ev.copy_found) {
            ev.copy_bits2 = lossless_mode_select::estimate_copy_bits(
                ev.copy_candidate, (int)pad_w, profile_id
            );
            // COPY (small-vector candidates only) always beats the minimum non-copy
            // token floors in current lossless mode model.
            const int non_copy_floor_bits2 =
                (profile_id == lossless_mode_select::PROFILE_PHOTO) ? 32 : 36;
            if (ev.copy_bits2 < non_copy_floor_bits2) {
                ev.copy_shortcut_forced = true;
                return ev;
            }
        }

        ev.filter_bits2 = lossless_mode_select::estimate_filter_bits(
            padded.data(), pad_w, pad_h, cur_x, cur_y, profile_id
        );

        if (!ev.copy_found) {
            Palette extracted_palette{};
            bool extracted_palette_valid = false;
            if (ev.unique_cnt <= mode_params.palette_max_colors) {
                extracted_palette = PaletteExtractor::extract(
                    ev.block.data(), mode_params.palette_max_colors
                );
                if (extracted_palette.size > 0 &&
                    extracted_palette.size <= mode_params.palette_max_colors) {
                    extracted_palette_valid = true;
                    bool transition_ok = (ev.transitions <= mode_params.palette_transition_limit) ||
                                         (extracted_palette.size <= 1);
                    bool variance_ok = ev.variance_proxy <= mode_params.palette_variance_limit;
                    if (transition_ok && variance_ok) {
                        ev.palette_found = true;
                        ev.palette_candidate = extracted_palette;
                        ev.palette_index_candidate =
                            PaletteExtractor::map_indices(ev.block.data(), ev.palette_candidate);
                        ev.palette_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (ev.palette_index_candidate[(size_t)k] !=
                                ev.palette_index_candidate[(size_t)k - 1]) {
                                ev.palette_transitions++;
                            }
                        }
                    }
                }
            }

            {
                int matches = 0;
                for (int q = 0; q < 4; q++) {
                    int qx = (q % 2) * 4;
                    int qy = (q / 2) * 4;
                    int cur_qx = cur_x + qx;
                    int cur_qy = cur_y + qy;

                    bool q_match_found = false;
                    for (int cand_idx = 0; cand_idx < 16; cand_idx++) {
                        const auto& cand = kTileMatch4Candidates[cand_idx];
                        int src_x = cur_qx + cand.dx;
                        int src_y = cur_qy + cand.dy;

                        if (src_x < 0 || src_y < 0 ||
                            src_x + 3 >= (int)pad_w || src_y + 3 >= (int)pad_h) {
                            continue;
                        }
                        if (!(src_y < cur_qy || (src_y == cur_qy && src_x < cur_qx))) continue;

                        bool match = true;
                        for (int dy = 0; dy < 4; dy++) {
                            const int16_t* dst_row =
                                &padded[(size_t)(cur_qy + dy) * (size_t)pad_w + (size_t)cur_qx];
                            const int16_t* src_row =
                                &padded[(size_t)(src_y + dy) * (size_t)pad_w + (size_t)src_x];
                            if (std::memcmp(dst_row, src_row, 4 * sizeof(int16_t)) != 0) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            ev.tile4_candidate.indices[q] = (uint8_t)cand_idx;
                            q_match_found = true;
                            break;
                        }
                    }
                    if (q_match_found) matches++;
                    else break;
                }
                ev.tile4_found = (matches == 4);
            }

            if (!ev.palette_found && profile_id != 2 && ev.unique_cnt <= 8) {
                Palette rescue_palette{};
                if (extracted_palette_valid) {
                    rescue_palette = extracted_palette;
                } else {
                    rescue_palette = PaletteExtractor::extract(ev.block.data(), 8);
                }
                if (rescue_palette.size > 0 && rescue_palette.size <= 8) {
                    ev.rescue_attempted_count++;
                    auto rescue_indices = PaletteExtractor::map_indices(ev.block.data(), rescue_palette);
                    int rescue_transitions = 0;
                    for (int k = 1; k < 64; k++) {
                        if (rescue_indices[(size_t)k] != rescue_indices[(size_t)k - 1]) {
                            rescue_transitions++;
                        }
                    }
                    int rescue_bits2 = lossless_mode_select::estimate_palette_bits(
                        rescue_palette, rescue_transitions, profile_id
                    );
                    if (profile_id == 1 && rescue_palette.size >= 2 && rescue_transitions <= 60) {
                        rescue_bits2 -= 24;
                    }
                    if (rescue_bits2 + 8 < ev.filter_bits2) {
                        ev.palette_found = true;
                        ev.palette_candidate = rescue_palette;
                        ev.palette_index_candidate = std::move(rescue_indices);
                        ev.palette_transitions = rescue_transitions;
                        ev.rescue_adopted = true;
                        ev.rescue_gain_bytes =
                            (uint64_t)std::max(0, (ev.filter_bits2 - rescue_bits2) / 2);
                    }
                }
            }
        }

        if (ev.tile4_found) ev.tile4_bits2 = 36;
        if (ev.copy_found) {
            ev.copy_bits2 = lossless_mode_select::estimate_copy_bits(
                ev.copy_candidate, (int)pad_w, profile_id
            );
        }

        if (ev.palette_found) {
            ev.palette_bits2 = lossless_mode_select::estimate_palette_bits(
                ev.palette_candidate, ev.palette_transitions, profile_id
            );
            if (profile_id == 1 && ev.palette_candidate.size >= 2 && ev.palette_transitions <= 60) {
                ev.palette_bits2 -= 24;
                ev.anime_palette_bonus_applied = true;
            }

            ev.rescue_bias_cond =
                (profile_id != 2) &&
                (ev.palette_candidate.size <= 8) &&
                (ev.unique_cnt <= 8) &&
                (ev.palette_transitions <= 32) &&
                (ev.variance_proxy >= 30000);
            if (ev.rescue_bias_cond) {
                ev.rescue_attempted_count++;
                ev.palette_bits2 -= 32;
            }
        }

        return ev;
    };
