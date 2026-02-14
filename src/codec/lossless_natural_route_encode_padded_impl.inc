// Natural/photo-oriented route:
// mode0: row SUB/UP/AVG + residual LZ+rANS(shared CDF)
// mode1: row SUB/UP/AVG/PAETH/MED + compressed predictor stream
// mode2: mode1 predictor set + natural-only global-chain LZ for residual stream
// mode3: mode1 predictor set + 2-context adaptive rANS (flat/edge)
template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile_padded(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr,
    int mode2_nice_length_override = -1,
    int mode2_match_strategy_override = -1
) {
    using Clock = std::chrono::steady_clock;
    auto ns_since = [](const Clock::time_point& t0, const Clock::time_point& t1) -> uint64_t {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };
    if (!padded || pad_w == 0 || pad_h == 0) return {};
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) return {};

    auto lz_params = detail::global_chain_lz_runtime_params();
    if (mode2_nice_length_override >= 4 && mode2_nice_length_override <= 255) {
        lz_params.nice_length = mode2_nice_length_override;
    }
    if (mode2_match_strategy_override >= 0 && mode2_match_strategy_override <= 2) {
        lz_params.match_strategy = mode2_match_strategy_override;
    }
    std::vector<uint8_t> mode0;
    std::vector<uint8_t> mode1;
    std::vector<uint8_t> mode2;
    std::vector<uint8_t> mode3;
    detail::Mode1Prepared mode1_prepared;
    detail::PackedPredictorStream mode1_pred;
    auto accumulate_mode2_lz = [&](const detail::GlobalChainLzCounters& c) {
        if (!stats) return;
        stats->natural_row_mode2_lz_calls += c.calls;
        stats->natural_row_mode2_lz_src_bytes_sum += c.src_bytes;
        stats->natural_row_mode2_lz_out_bytes_sum += c.out_bytes;
        stats->natural_row_mode2_lz_match_count += c.match_count;
        stats->natural_row_mode2_lz_match_bytes_sum += c.match_bytes;
        stats->natural_row_mode2_lz_literal_bytes_sum += c.literal_bytes;
        stats->natural_row_mode2_lz_chain_steps_sum += c.chain_steps;
        stats->natural_row_mode2_lz_depth_limit_hits += c.depth_limit_hits;
        stats->natural_row_mode2_lz_early_maxlen_hits += c.early_maxlen_hits;
        stats->natural_row_mode2_lz_nice_cutoff_hits += c.nice_cutoff_hits;
        stats->natural_row_mode2_lz_len3_reject_dist += c.len3_reject_dist;
        stats->natural_row_mode2_lz_optparse_enabled += c.optparse_enabled;
        stats->natural_row_mode2_lz_optparse_fallback_count += c.optparse_fallback_count;
        stats->natural_row_mode2_lz_optparse_fallback_memcap += c.optparse_fallback_memcap;
        stats->natural_row_mode2_lz_optparse_fallback_allocfail += c.optparse_fallback_allocfail;
        stats->natural_row_mode2_lz_optparse_fallback_unreachable +=
            c.optparse_fallback_unreachable;
        stats->natural_row_mode2_lz_optparse_dp_positions_sum += c.optparse_dp_positions;
        stats->natural_row_mode2_lz_optparse_lit_edges_sum += c.optparse_lit_edges_eval;
        stats->natural_row_mode2_lz_optparse_match_edges_sum += c.optparse_match_edges_eval;
        stats->natural_row_mode2_lz_optparse_tokens_lit_sum += c.optparse_tokens_litrun;
        stats->natural_row_mode2_lz_optparse_tokens_match_sum += c.optparse_tokens_match;
        stats->natural_row_mode2_lz_optparse_shorter_than_longest_sum +=
            c.optparse_chose_shorter_than_longest;
        stats->natural_row_mode2_lz_optparse_probe_accept += c.optparse_probe_accept;
        stats->natural_row_mode2_lz_optparse_probe_reject += c.optparse_probe_reject;
        stats->natural_row_mode2_lz_optparse_adopt += c.optparse_adopt;
        stats->natural_row_mode2_lz_optparse_reject_small_gain +=
            c.optparse_reject_small_gain;
    };
    constexpr uint32_t kPrepParallelPixelThreshold = 262144u;
    thread_budget::ScopedThreadTokens pipeline_tokens;
    if (pixel_count >= kPrepParallelPixelThreshold) {
        pipeline_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(1);
    }

    if (pipeline_tokens.acquired()) {
        struct ReadyData {
            std::shared_ptr<const detail::Mode1Prepared> prepared;
            std::shared_ptr<const detail::PackedPredictorStream> pred;
            uint64_t prep_ns = 0;
            uint64_t pred_ns = 0;
        };
        struct TimedPayload {
            std::vector<uint8_t> payload;
            uint64_t elapsed_ns = 0;
            detail::GlobalChainLzCounters lz;
        };
        struct TimedMode23 {
            TimedPayload mode2;
            std::vector<uint8_t> mode3;
            uint64_t mode3_elapsed_ns = 0;
        };
        if (stats) {
            stats->natural_row_prep_parallel_count++;
            stats->natural_row_prep_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
            stats->natural_row_mode12_parallel_count++;
            stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
        }

        std::promise<ReadyData> ready_promise;
        auto ready_future = ready_promise.get_future();
        auto mode23_future = std::async(
            std::launch::async,
            [&,
             rp = std::move(ready_promise)]() mutable -> TimedMode23 {
                thread_budget::ScopedParallelRegion guard;
                ReadyData ready;
                const auto t_prep0 = Clock::now();
                auto prep_local = detail::build_mode1_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    zigzag_encode_val
                );
                const auto t_prep1 = Clock::now();
                ready.prep_ns = ns_since(t_prep0, t_prep1);

                const auto t_pred0 = Clock::now();
                auto pred_local = detail::build_packed_predictor_stream(
                    prep_local.row_pred_ids,
                    encode_byte_stream
                );
                const auto t_pred1 = Clock::now();
                ready.pred_ns = ns_since(t_pred0, t_pred1);
                ready.prepared = std::make_shared<const detail::Mode1Prepared>(std::move(prep_local));
                ready.pred = std::make_shared<const detail::PackedPredictorStream>(std::move(pred_local));

                rp.set_value(ready);

                const auto t_mode2_0 = Clock::now();
                TimedPayload out2;
                detail::GlobalChainLzCounters lz_counters;
                out2.payload = detail::build_mode1_payload_from_prepared(
                    *ready.prepared,
                    *ready.pred,
                    pad_h,
                    pixel_count,
                    encode_byte_stream_shared_lz,
                    2,
                    [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                        return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                    }
                );
                out2.lz = lz_counters;
                const auto t_mode2_1 = Clock::now();
                out2.elapsed_ns = ns_since(t_mode2_0, t_mode2_1);

                const auto t_mode3_0 = Clock::now();
                std::vector<uint8_t> out3 = detail::build_mode3_payload_from_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    *ready.prepared,
                    *ready.pred,
                    encode_byte_stream
                );
                const auto t_mode3_1 = Clock::now();

                return TimedMode23{
                    std::move(out2),
                    std::move(out3),
                    ns_since(t_mode3_0, t_mode3_1)
                };
            }
        );

        const auto t_mode0_0 = Clock::now();
        mode0 = detail::build_mode0_payload(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val, encode_byte_stream_shared_lz
        );
        const auto t_mode0_1 = Clock::now();
        if (stats) stats->natural_row_mode0_build_ns += ns_since(t_mode0_0, t_mode0_1);

        auto ready = ready_future.get();
        if (stats) {
            stats->natural_row_mode1_prepare_ns += ready.prep_ns;
            stats->natural_row_pred_pack_ns += ready.pred_ns;
            if (ready.pred->mode == 0) stats->natural_row_pred_mode_raw_count++;
            else stats->natural_row_pred_mode_rans_count++;
        }

        const auto t_mode1_0 = Clock::now();
        mode1 = detail::build_mode1_payload_from_prepared(
            *ready.prepared,
            *ready.pred,
            pad_h,
            pixel_count,
            encode_byte_stream_shared_lz,
            1,
            [](const std::vector<uint8_t>& bytes) {
                return TileLZ::compress(bytes);
            }
        );
        const auto t_mode1_1 = Clock::now();
        if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);

        auto mode23_res = mode23_future.get();
        mode2 = std::move(mode23_res.mode2.payload);
        mode3 = std::move(mode23_res.mode3);
        if (stats) {
            stats->natural_row_mode2_build_ns += mode23_res.mode2.elapsed_ns;
            stats->natural_row_mode3_build_ns += mode23_res.mode3_elapsed_ns;
        }
        accumulate_mode2_lz(mode23_res.mode2.lz);
    } else {
        if (stats) stats->natural_row_prep_seq_count++;
        const auto t_mode0_0 = Clock::now();
        mode0 = detail::build_mode0_payload(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val, encode_byte_stream_shared_lz
        );
        const auto t_mode0_1 = Clock::now();
        if (stats) stats->natural_row_mode0_build_ns += ns_since(t_mode0_0, t_mode0_1);
        const auto t_mode1p_0 = Clock::now();
        mode1_prepared = detail::build_mode1_prepared(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val
        );
        const auto t_mode1p_1 = Clock::now();
        if (stats) stats->natural_row_mode1_prepare_ns += ns_since(t_mode1p_0, t_mode1p_1);
        const auto t_pred0 = Clock::now();
        mode1_pred = detail::build_packed_predictor_stream(
            mode1_prepared.row_pred_ids,
            encode_byte_stream
        );
        const auto t_pred1 = Clock::now();
        if (stats) {
            stats->natural_row_pred_pack_ns += ns_since(t_pred0, t_pred1);
            if (mode1_pred.mode == 0) stats->natural_row_pred_mode_raw_count++;
            else stats->natural_row_pred_mode_rans_count++;
        }

        const size_t mode2_min_size = detail::mode12_min_candidate_size(mode1_pred);
        const uint64_t mode2_limit_vs_mode0 =
            ((uint64_t)mode0.size() * (uint64_t)lz_params.bias_permille) / 1000ull;
        const bool mode2_possible_vs_mode0 =
            (mode2_min_size != std::numeric_limits<size_t>::max()) &&
            (mode2_min_size <= mode2_limit_vs_mode0);

        constexpr uint32_t kMode12ParallelPixelThreshold = 262144u;
        thread_budget::ScopedThreadTokens mode12_tokens;
        if (pixel_count >= kMode12ParallelPixelThreshold) {
            mode12_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(1);
        }
        struct TimedPayload {
            std::vector<uint8_t> payload;
            uint64_t elapsed_ns = 0;
            detail::GlobalChainLzCounters lz;
        };
        struct TimedMode23 {
            TimedPayload mode2;
            std::vector<uint8_t> mode3;
            uint64_t mode3_elapsed_ns = 0;
        };
        if (mode12_tokens.acquired() && mode2_possible_vs_mode0) {
            if (stats) {
                stats->natural_row_mode12_parallel_count++;
                stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)mode12_tokens.count();
            }
            auto f_mode23 = std::async(std::launch::async, [&]() -> TimedMode23 {
                thread_budget::ScopedParallelRegion guard;
                const auto t0 = Clock::now();
                TimedPayload out2;
                detail::GlobalChainLzCounters lz_counters;
                out2.payload = detail::build_mode1_payload_from_prepared(
                    mode1_prepared,
                    mode1_pred,
                    pad_h,
                    pixel_count,
                    encode_byte_stream_shared_lz,
                    2,
                    [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                        return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                    }
                );
                out2.lz = lz_counters;
                const auto t1 = Clock::now();
                out2.elapsed_ns = ns_since(t0, t1);

                const auto t_mode3_0 = Clock::now();
                std::vector<uint8_t> out3 = detail::build_mode3_payload_from_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    mode1_prepared,
                    mode1_pred,
                    encode_byte_stream
                );
                const auto t_mode3_1 = Clock::now();
                return TimedMode23{
                    std::move(out2),
                    std::move(out3),
                    ns_since(t_mode3_0, t_mode3_1)
                };
            });
            const auto t_mode1_0 = Clock::now();
            mode1 = detail::build_mode1_payload_from_prepared(
                mode1_prepared,
                mode1_pred,
                pad_h,
                pixel_count,
                encode_byte_stream_shared_lz,
                1,
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                }
            );
            const auto t_mode1_1 = Clock::now();
            if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);
            auto mode23_res = f_mode23.get();
            mode2 = std::move(mode23_res.mode2.payload);
            mode3 = std::move(mode23_res.mode3);
            if (stats) {
                stats->natural_row_mode2_build_ns += mode23_res.mode2.elapsed_ns;
                stats->natural_row_mode3_build_ns += mode23_res.mode3_elapsed_ns;
            }
            accumulate_mode2_lz(mode23_res.mode2.lz);
            if (stats && mode2.empty()) stats->natural_row_mode2_bias_reject_count++;
        } else {
            if (stats) stats->natural_row_mode12_seq_count++;
            const auto t_mode1_0 = Clock::now();
            mode1 = detail::build_mode1_payload_from_prepared(
                mode1_prepared,
                mode1_pred,
                pad_h,
                pixel_count,
                encode_byte_stream_shared_lz,
                1,
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                }
            );
            const auto t_mode1_1 = Clock::now();
            if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);
            if (mode2_possible_vs_mode0) {
                const uint64_t best_after_mode1 =
                    std::min<uint64_t>((uint64_t)mode0.size(), (uint64_t)mode1.size());
                const uint64_t mode2_limit_vs_best =
                    (best_after_mode1 * (uint64_t)lz_params.bias_permille) / 1000ull;
                const bool mode2_possible_vs_best =
                    (mode2_min_size <= mode2_limit_vs_best);
                if (mode2_possible_vs_best) {
                    const auto t_mode2_0 = Clock::now();
                    detail::GlobalChainLzCounters lz_counters;
                    mode2 = detail::build_mode1_payload_from_prepared(
                        mode1_prepared,
                        mode1_pred,
                        pad_h,
                        pixel_count,
                        encode_byte_stream_shared_lz,
                        2,
                        [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                            return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                        }
                    );
                    const auto t_mode2_1 = Clock::now();
                    if (stats) stats->natural_row_mode2_build_ns += ns_since(t_mode2_0, t_mode2_1);
                    accumulate_mode2_lz(lz_counters);
                    if (stats && mode2.empty()) stats->natural_row_mode2_bias_reject_count++;
                } else if (stats) {
                    stats->natural_row_mode2_bias_reject_count++;
                }
            } else if (stats) {
                stats->natural_row_mode2_bias_reject_count++;
            }
            const auto t_mode3_0 = Clock::now();
            mode3 = detail::build_mode3_payload_from_prepared(
                padded, pad_w, pad_h, pixel_count,
                mode1_prepared,
                mode1_pred,
                encode_byte_stream
            );
            const auto t_mode3_1 = Clock::now();
            if (stats) stats->natural_row_mode3_build_ns += ns_since(t_mode3_0, t_mode3_1);
        }
    }
    if (mode0.empty()) return {};
    if (stats) {
        stats->natural_row_mode0_size_sum += (uint64_t)mode0.size();
        stats->natural_row_mode1_size_sum += (uint64_t)mode1.size();
        stats->natural_row_mode2_size_sum += (uint64_t)mode2.size();
        stats->natural_row_mode3_size_sum += (uint64_t)mode3.size();
    }

    uint8_t selected_mode = 0;
    std::vector<uint8_t> best = std::move(mode0);
    if (!mode1.empty() && mode1.size() < best.size()) {
        best = std::move(mode1);
        selected_mode = 1;
    }
    if (!mode2.empty()) {
        const uint64_t lhs = (uint64_t)mode2.size() * 1000ull;
        const uint64_t rhs = (uint64_t)best.size() * (uint64_t)lz_params.bias_permille;
        if (lhs <= rhs) {
            best = std::move(mode2);
            selected_mode = 2;
            if (stats) stats->natural_row_mode2_bias_adopt_count++;
        } else if (stats) {
            stats->natural_row_mode2_bias_reject_count++;
        }
    }
    if (!mode3.empty() && mode3.size() < best.size()) {
        best = std::move(mode3);
        selected_mode = 3;
    }
    if (stats) {
        if (selected_mode == 0) stats->natural_row_mode0_selected_count++;
        else if (selected_mode == 1) stats->natural_row_mode1_selected_count++;
        else if (selected_mode == 2) stats->natural_row_mode2_selected_count++;
        else stats->natural_row_mode3_selected_count++;
    }
    return best;
}
