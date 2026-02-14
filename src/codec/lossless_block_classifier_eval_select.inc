    const bool diag_palette16 = enable_filter_diag_palette16();
    FileHeader::BlockType prev_mode = FileHeader::BlockType::DCT;

    for (int i = 0; i < nb; i++) {
        auto& ev = evals[(size_t)i];
        if (ev.copy_shortcut_forced) {
            out.block_types[(size_t)i] = FileHeader::BlockType::COPY;
            prev_mode = FileHeader::BlockType::COPY;
            out.copy_ops.push_back(ev.copy_candidate);
            if (stats) {
                stats->class_copy_shortcut_selected++;
                stats->total_blocks++;
                stats->copy_candidates++;
                stats->copy_selected++;
                stats->est_copy_bits_sum += (uint64_t)(ev.copy_bits2 / 2);
                stats->est_selected_bits_sum += (uint64_t)(ev.copy_bits2 / 2);
            }
            continue;
        }

        int tile4_bits2 = ev.tile4_bits2;
        int copy_bits2 = ev.copy_bits2;
        int palette_bits2 = ev.palette_bits2;
        int filter_bits2 = ev.filter_bits2;

        if (profile_id == 2) {
            if (ev.tile4_found && prev_mode == FileHeader::BlockType::TILE_MATCH4) tile4_bits2 -= 4;
            if (ev.copy_found && prev_mode == FileHeader::BlockType::COPY) copy_bits2 -= 4;
            if (ev.palette_found && prev_mode == FileHeader::BlockType::PALETTE) palette_bits2 -= 4;
            if (prev_mode == FileHeader::BlockType::DCT) filter_bits2 -= 4;
        }

        if (stats) {
            stats->palette_rescue_attempted += (uint64_t)ev.rescue_attempted_count;
            if (ev.rescue_adopted) {
                stats->palette_rescue_adopted++;
                stats->palette_rescue_gain_bits_sum += ev.rescue_gain_bytes;
            }
            if (ev.anime_palette_bonus_applied) {
                stats->anime_palette_bonus_applied++;
            }

            stats->total_blocks++;
            stats->est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);
            if (ev.tile4_found) {
                stats->tile4_candidates++;
                stats->est_tile4_bits_sum += (uint64_t)(tile4_bits2 / 2);
            }
            if (ev.copy_found) {
                stats->copy_candidates++;
                stats->est_copy_bits_sum += (uint64_t)(copy_bits2 / 2);
            }
            if (ev.palette_found) {
                stats->palette_candidates++;
                stats->est_palette_bits_sum += (uint64_t)(palette_bits2 / 2);
            }
            if (ev.copy_found && ev.palette_found) stats->copy_palette_overlap++;
        }

        FileHeader::BlockType best_mode = FileHeader::BlockType::DCT;
        if (tile4_bits2 <= copy_bits2 && tile4_bits2 <= palette_bits2 && tile4_bits2 <= filter_bits2) {
            best_mode = FileHeader::BlockType::TILE_MATCH4;
        } else if (copy_bits2 <= palette_bits2 && copy_bits2 <= filter_bits2) {
            best_mode = FileHeader::BlockType::COPY;
        } else if (palette_bits2 <= filter_bits2) {
            best_mode = FileHeader::BlockType::PALETTE;
        }

        int selected_bits2 = filter_bits2;
        if (best_mode == FileHeader::BlockType::TILE_MATCH4) selected_bits2 = tile4_bits2;
        else if (best_mode == FileHeader::BlockType::COPY) selected_bits2 = copy_bits2;
        else if (best_mode == FileHeader::BlockType::PALETTE) selected_bits2 = palette_bits2;

        if (stats) {
            if (ev.tile4_found && best_mode != FileHeader::BlockType::TILE_MATCH4) {
                if (best_mode == FileHeader::BlockType::COPY) stats->tile4_rejected_by_copy++;
                else if (best_mode == FileHeader::BlockType::PALETTE) stats->tile4_rejected_by_palette++;
                else stats->tile4_rejected_by_filter++;
                stats->est_tile4_loss_bits_sum +=
                    (uint64_t)(std::max(0, tile4_bits2 - selected_bits2) / 2);
            }
            if (ev.copy_found && best_mode != FileHeader::BlockType::COPY) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) stats->copy_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::PALETTE) stats->copy_rejected_by_palette++;
                else stats->copy_rejected_by_filter++;
                stats->est_copy_loss_bits_sum +=
                    (uint64_t)(std::max(0, copy_bits2 - selected_bits2) / 2);
            }
            if (ev.palette_found && best_mode != FileHeader::BlockType::PALETTE) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) stats->palette_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::COPY) stats->palette_rejected_by_copy++;
                else stats->palette_rejected_by_filter++;
                stats->est_palette_loss_bits_sum +=
                    (uint64_t)(std::max(0, palette_bits2 - selected_bits2) / 2);
            }
        }

        out.block_types[(size_t)i] = best_mode;
        prev_mode = best_mode;
        if (best_mode == FileHeader::BlockType::TILE_MATCH4) {
            out.tile4_results.push_back(ev.tile4_candidate);
            if (stats) {
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                stats->tile4_selected++;
            }
        } else if (best_mode == FileHeader::BlockType::COPY) {
            out.copy_ops.push_back(ev.copy_candidate);
            if (stats) {
                stats->copy_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
            }
        } else if (best_mode == FileHeader::BlockType::PALETTE) {
            out.palettes.push_back(ev.palette_candidate);
            out.palette_indices.push_back(std::move(ev.palette_index_candidate));
            if (stats) {
                stats->palette_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                if (ev.rescue_bias_cond) {
                    stats->palette_rescue_adopted++;
                    stats->palette_rescue_gain_bits_sum += 16;
                }
            }
        } else {
            if (stats) {
                stats->filter_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);

                if (ev.copy_found) stats->filter_blocks_with_copy_candidate++;
                if (ev.palette_found) stats->filter_blocks_with_palette_candidate++;
                if (ev.unique_cnt <= 2) stats->filter_blocks_unique_le2++;
                else if (ev.unique_cnt <= 4) stats->filter_blocks_unique_le4++;
                else if (ev.unique_cnt <= 8) stats->filter_blocks_unique_le8++;
                else stats->filter_blocks_unique_gt8++;
                stats->filter_blocks_transitions_sum += (uint64_t)ev.transitions;
                stats->filter_blocks_variance_proxy_sum +=
                    (uint64_t)std::max<int64_t>(0, ev.variance_proxy);
                stats->filter_blocks_est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);

                if (diag_palette16 && ev.unique_cnt <= 8) {
                    Palette diag_palette16_res = PaletteExtractor::extract(ev.block.data(), 8);
                    if (diag_palette16_res.size > 0 && diag_palette16_res.size <= 8) {
                        auto diag_indices =
                            PaletteExtractor::map_indices(ev.block.data(), diag_palette16_res);
                        int diag_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (diag_indices[(size_t)k] != diag_indices[(size_t)k - 1]) {
                                diag_transitions++;
                            }
                        }
                        int diag_palette_bits2 = lossless_mode_select::estimate_palette_bits(
                            diag_palette16_res, diag_transitions, profile_id
                        );
                        if (profile_id == 1 &&
                            diag_palette16_res.size >= 2 && diag_transitions <= 60) {
                            diag_palette_bits2 -= 24;
                        }
                        stats->filter_diag_palette16_candidates++;
                        stats->filter_diag_palette16_size_sum += diag_palette16_res.size;
                        stats->filter_diag_palette16_est_bits_sum +=
                            (uint64_t)(diag_palette_bits2 / 2);
                        if (diag_palette_bits2 < filter_bits2) {
                            stats->filter_diag_palette16_better++;
                            stats->filter_diag_palette16_gain_bits_sum +=
                                (uint64_t)((filter_bits2 - diag_palette_bits2) / 2);
                        }
                    }
                }
            }
        }
    }

    return out;
