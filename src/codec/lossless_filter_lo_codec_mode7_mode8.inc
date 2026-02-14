        // Mode7: context-split wrapper with per-context coder selection.
        // Each context chooses smaller of: legacy adaptive CDF vs shared LZ CDF.
        const bool mode7_enable = get_mode7_enable();
        if (mode7_enable && !mode4_aborted && nonempty_ctx >= 2) {
            if (stats) stats->filter_lo_mode7_candidates++;
            const auto t_mode7_eval0 = Clock::now();

            std::vector<std::vector<uint8_t>> mode7_candidate_streams = ctx_streams;
            std::vector<uint32_t> mode7_candidate_ctx_raw_counts = ctx_raw_counts;
            uint32_t mode7_candidate_shared_mask = 0;
            uint64_t mode7_shared_ctx_count = 0;
            size_t mode7_sz = 1 + 1 + 4 + 4 + 6 * 4;

            for (int k = 0; k < 6; k++) {
                if (lo_ctx[k].empty()) continue;
                auto& selected_stream = mode7_candidate_streams[k];
                if (lo_ctx[k].size() >= (size_t)mode7_params.min_ctx_bytes) {
                    auto shared_stream = encode_byte_stream_shared_lz(lo_ctx[k]);
                    if (shared_stream.size() < selected_stream.size()) {
                        selected_stream = std::move(shared_stream);
                        mode7_candidate_shared_mask |= (1u << k);
                        mode7_shared_ctx_count++;
                    }
                }
                mode7_sz += selected_stream.size();
            }

            if (stats) {
                stats->filter_lo_mode7_eval_ns += ns_since(t_mode7_eval0, Clock::now());
                stats->filter_lo_mode7_wrapped_bytes_sum += mode7_sz;
                stats->filter_lo_mode7_legacy_bytes_sum += legacy_size;
                stats->filter_lo_mode7_shared_ctx_sum += mode7_shared_ctx_count;
            }

            const bool mode7_better_than_legacy =
                (mode7_sz * 1000 <= legacy_size * (size_t)mode7_params.gain_permille);
            const bool mode7_better_than_mode4 =
                (mode4_sz != std::numeric_limits<size_t>::max()) &&
                (mode7_sz * 1000 <= mode4_sz * (size_t)mode7_params.vs_mode4_permille);

            if (mode7_better_than_legacy && mode7_better_than_mode4) {
                if (mode7_sz < best_size) {
                    best_size = mode7_sz;
                    best_mode = 7;
                    mode7_streams = std::move(mode7_candidate_streams);
                    mode7_ctx_raw_counts = std::move(mode7_candidate_ctx_raw_counts);
                    mode7_shared_mask = mode7_candidate_shared_mask;
                } else {
                    if (stats) stats->filter_lo_mode7_reject_best++;
                }
            } else {
                if (stats) stats->filter_lo_mode7_reject_gate++;
            }
        }

        // Mode8: context-split wrapper with per-context hybrid codec selection.
        // Each context chooses smallest of: legacy rANS / delta+rANS / LZ+rANS(shared).
        const bool mode8_enable = get_mode8_enable();
        const bool mode4_valid = !mode4_aborted && nonempty_ctx >= 2;
        if (mode8_enable && mode4_valid) {
            if (stats) stats->filter_lo_mode8_candidates++;
            const auto t_mode8_eval0 = Clock::now();
            const auto& mode8_params = get_mode8_runtime_params();

            std::vector<std::vector<uint8_t>> mode8_streams(6);
            std::vector<uint8_t> mode8_codec_ids(6, 255);  // 0=legacy, 1=delta, 2=lz, 255=empty
            std::vector<uint32_t> mode8_ctx_raw_counts = ctx_raw_counts;
            uint64_t mode8_ctx_legacy = 0, mode8_ctx_delta = 0, mode8_ctx_lz = 0;
            size_t mode8_sz = 1 + 1 + 4 + 6 + 6 * 4;  // header + codec_ids + lens
            bool mode8_aborted = false;

            for (int k = 0; k < 6 && !mode8_aborted; k++) {
                if (lo_ctx[k].empty()) {
                    mode8_codec_ids[k] = 255;
                    continue;
                }

                // Candidate 0: legacy rANS
                auto legacy_stream = encode_byte_stream(lo_ctx[k]);
                size_t best_ctx_sz = legacy_stream.size();
                uint8_t best_codec = 0;

                // Candidate 1: delta + rANS
                std::vector<uint8_t> delta_data(lo_ctx[k].size());
                delta_data[0] = lo_ctx[k][0];
                for (size_t i = 1; i < lo_ctx[k].size(); i++) {
                    delta_data[i] = (uint8_t)(lo_ctx[k][i] - lo_ctx[k][i - 1]);
                }
                auto delta_stream = encode_byte_stream(delta_data);
                if (delta_stream.size() < best_ctx_sz) {
                    best_ctx_sz = delta_stream.size();
                    best_codec = 1;
                    mode8_streams[k] = std::move(delta_stream);
                } else {
                    mode8_streams[k] = std::move(legacy_stream);
                }

                // Candidate 2: LZ + rANS(shared) - only for larger contexts
                if (lo_ctx[k].size() >= (size_t)mode8_params.min_ctx_bytes) {
                    auto ctx_lz = compress_lz(lo_ctx[k]);
                    if (!ctx_lz.empty()) {
                        auto lz_stream = encode_byte_stream_shared_lz(ctx_lz);
                        if (lz_stream.size() < best_ctx_sz) {
                            best_ctx_sz = lz_stream.size();
                            best_codec = 2;
                            mode8_streams[k] = std::move(lz_stream);
                        }
                    }
                }

                mode8_codec_ids[k] = best_codec;
                mode8_sz += best_ctx_sz;

                // Track codec selection stats
                if (best_codec == 0) mode8_ctx_legacy++;
                else if (best_codec == 1) mode8_ctx_delta++;
                else if (best_codec == 2) mode8_ctx_lz++;

                // Early abort if size exceeds limits
                if (mode8_sz > best_size) {
                    mode8_aborted = true;
                }
            }

            if (stats) {
                stats->filter_lo_mode8_eval_ns += ns_since(t_mode8_eval0, Clock::now());
                if (!mode8_aborted) {
                    stats->filter_lo_mode8_wrapped_bytes_sum += mode8_sz;
                    stats->filter_lo_mode8_ctx_legacy_sum += mode8_ctx_legacy;
                    stats->filter_lo_mode8_ctx_delta_sum += mode8_ctx_delta;
                    stats->filter_lo_mode8_ctx_lz_sum += mode8_ctx_lz;
                }
            }

            if (!mode8_aborted) {
                const bool mode8_better_than_legacy =
                    (mode8_sz * 1000 <= legacy_size * (size_t)mode8_params.gain_permille);
                const bool mode8_better_than_mode4 =
                    (mode8_sz * 1000 <= mode4_sz * (size_t)mode8_params.vs_mode4_permille);

                if (mode8_better_than_legacy && mode8_better_than_mode4) {
                    if (mode8_sz < best_size) {
                        best_size = mode8_sz;
                        best_mode = 8;
                        // Store mode8 streams for final output
                        mode8_output_streams = std::move(mode8_streams);
                        mode8_output_codec_ids = std::move(mode8_codec_ids);
                        mode8_output_ctx_raw_counts = std::move(mode8_ctx_raw_counts);
                    } else {
                        if (stats) stats->filter_lo_mode8_reject_best++;
                    }
                } else {
                    if (stats) stats->filter_lo_mode8_reject_gate++;
                }
            }
        }
