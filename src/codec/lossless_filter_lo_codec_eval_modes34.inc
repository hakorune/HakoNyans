    std::vector<uint8_t> pred_stream;
    std::vector<uint8_t> resid_stream;
    std::vector<uint8_t> mode3_preds;
    std::vector<int> row_lens;

    std::vector<std::vector<uint8_t>> mode4_streams(6);
    std::vector<uint32_t> mode4_ctx_raw_counts(6, 0);
    std::vector<std::vector<uint8_t>> mode7_streams(6);
    std::vector<uint32_t> mode7_ctx_raw_counts(6, 0);
    uint32_t mode7_shared_mask = 0;
    // Mode8 output variables (declared here for use in output stage)
    std::vector<std::vector<uint8_t>> mode8_output_streams;
    std::vector<uint8_t> mode8_output_codec_ids;
    std::vector<uint32_t> mode8_output_ctx_raw_counts;

    const bool enable_mode3_mode4 = ((profile_code == 1 || profile_code == 2) && lo_bytes.size() > 256);
    if (enable_mode3_mode4) {
        std::vector<int> dct_row_lens(std::max(1u, pad_h / 8u), 0);
        for (uint32_t by = 0; by < pad_h / 8u; by++) {
            int dct_cols = 0;
            const size_t row_off = (size_t)by * (size_t)nx;
            for (int bx = 0; bx < nx; bx++) {
                if (block_types[row_off + (size_t)bx] == FileHeader::BlockType::DCT) dct_cols++;
            }
            dct_row_lens[(size_t)by] = dct_cols * 8;
        }
        row_lens.assign(pad_h, 0);
        for (uint32_t y = 0; y < pad_h; y++) {
            row_lens[y] = dct_row_lens[(size_t)(y / 8)];
        }

        std::vector<uint8_t> preds;
        std::vector<uint8_t> resids;
        size_t active_rows = 0;
        for (int len : row_lens) if (len > 0) active_rows++;
        const size_t mode3_min_size = 1 + 1 + 4 + 4 + (2 * kByteStreamMinEncodedBytes);
        const bool mode3_lower_bound_reject =
            (mode3_min_size >= best_size) ||
            ((uint64_t)mode3_min_size * 1000ull >
             (uint64_t)legacy_size * (uint64_t)kFilterLoModeWrapperGainPermilleDefault);

        if (!mode3_lower_bound_reject) {
            const auto t_mode3_eval0 = Clock::now();
            preds.reserve(active_rows);
            resids.resize(lo_bytes.size());

            size_t offset = 0;
            size_t prev_valid_row_start = 0;
            size_t prev_valid_row_len = 0;
            size_t resid_write = 0;

            for (uint32_t y = 0; y < pad_h; y++) {
                int len = row_lens[y];
                if (len == 0) continue;

                const uint8_t* curr_row = &lo_bytes[offset];
                int best_p = 0;
                int64_t min_cost = -1;

                for (int p = 0; p < 4; p++) {
                    int64_t cost = 0;
                    for (int i = 0; i < len; i++) {
                        uint8_t pred_val = 0;
                        if (p == 1) {
                            pred_val = (i == 0) ? 0 : curr_row[i - 1];
                        } else if (p == 2) {
                            pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                        } else if (p == 3) {
                            uint8_t left = (i == 0) ? 0 : curr_row[i - 1];
                            uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                            pred_val = (left + up) / 2;
                        }
                        int diff = (int)curr_row[i] - pred_val;
                        if (diff < 0) diff += 256;
                        if (diff > 128) diff = 256 - diff;
                        cost += diff;
                        if (min_cost != -1 && cost >= min_cost) break;
                    }
                    if (min_cost == -1 || cost < min_cost) {
                        min_cost = cost;
                        best_p = p;
                    }
                }

                preds.push_back((uint8_t)best_p);

                for (int i = 0; i < len; i++) {
                    uint8_t pred_val = 0;
                    if (best_p == 1) pred_val = (i == 0) ? 0 : curr_row[i - 1];
                    else if (best_p == 2) pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                    else if (best_p == 3) {
                        uint8_t left = (i == 0) ? 0 : curr_row[i - 1];
                        uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                        pred_val = (left + up) / 2;
                    }
                    resids[resid_write++] = (uint8_t)(curr_row[i] - pred_val);
                }

                prev_valid_row_start = offset;
                prev_valid_row_len = len;
                offset += len;
            }
            resids.resize(resid_write);

            auto preds_enc = encode_byte_stream(preds);
            auto resids_enc = encode_byte_stream(resids);
            size_t total_sz = 1 + 1 + 4 + 4 + preds_enc.size() + resids_enc.size();
            if (stats) stats->filter_lo_mode3_eval_ns += ns_since(t_mode3_eval0, Clock::now());

            if (total_sz < best_size && total_sz * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
                best_size = total_sz;
                best_mode = 3;
                pred_stream = std::move(preds_enc);
                resid_stream = std::move(resids_enc);
                mode3_preds = std::move(preds);
            }
        }

        const auto t_mode4_eval0 = Clock::now();
        std::vector<std::vector<uint8_t>> lo_ctx(6);
        std::array<uint32_t, 6> ctx_reserved = {0, 0, 0, 0, 0, 0};
        for (uint32_t y = 0; y < pad_h; y++) {
            int len = row_lens[y];
            if (len <= 0) continue;
            uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
            if (fid > 5) fid = 0;
            ctx_reserved[fid] += (uint32_t)len;
        }
        for (int k = 0; k < 6; k++) lo_ctx[k].reserve((size_t)ctx_reserved[k]);

        size_t off = 0;
        for (uint32_t y = 0; y < pad_h; y++) {
            int len = row_lens[y];
            if (len <= 0) continue;
            size_t end_off = std::min(off + (size_t)len, lo_bytes.size());
            if (end_off <= off) break;
            uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
            if (fid > 5) fid = 0;
            size_t take = end_off - off;
            auto& dst = lo_ctx[fid];
            size_t dst_off = dst.size();
            dst.resize(dst_off + take);
            std::memcpy(dst.data() + dst_off, lo_bytes.data() + off, take);
            off = end_off;
        }

        size_t mode4_sz = 1 + 1 + 4 + 6 * 4;
        int nonempty_ctx = 0;
        for (int k = 0; k < 6; k++) {
            if (!lo_ctx[k].empty()) nonempty_ctx++;
        }
        const size_t mode4_min_size = mode4_sz + (size_t)nonempty_ctx * kByteStreamMinEncodedBytes;
        const bool mode4_lower_bound_reject =
            (nonempty_ctx < 2) ||
            (mode4_min_size >= best_size) ||
            ((uint64_t)mode4_min_size * 1000ull >
             (uint64_t)legacy_size * (uint64_t)kFilterLoModeWrapperGainPermilleDefault);

        const size_t mode4_gate_limit =
            (legacy_size * (size_t)kFilterLoModeWrapperGainPermilleDefault + 999u) / 1000u;
        std::vector<std::vector<uint8_t>> ctx_streams(6);
        std::vector<uint32_t> ctx_raw_counts(6, 0);
        bool mode4_aborted = mode4_lower_bound_reject;
        thread_budget::ScopedThreadTokens ctx_parallel_tokens;
        if (!mode4_aborted && hw_threads >= 6 && lo_bytes.size() >= 8192) {
            ctx_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(6);
        }
        const bool allow_parallel_ctx = (!mode4_aborted && ctx_parallel_tokens.acquired());
        if (allow_parallel_ctx) {
            std::vector<std::future<std::vector<uint8_t>>> futs(6);
            std::vector<bool> launched(6, false);
            for (int k = 0; k < 6; k++) {
                ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
                if (lo_ctx[k].empty()) continue;
                launched[k] = true;
                futs[k] = worker_pool.submit([&, k]() {
                    thread_budget::ScopedParallelRegion guard;
                    return encode_byte_stream(lo_ctx[k]);
                });
            }
            for (int k = 0; k < 6; k++) {
                if (launched[k]) {
                    ctx_streams[k] = futs[k].get();
                }
                mode4_sz += ctx_streams[k].size();
            }
        } else if (!mode4_aborted) {
            for (int k = 0; k < 6; k++) {
                ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
                if (!lo_ctx[k].empty()) {
                    ctx_streams[k] = encode_byte_stream(lo_ctx[k]);
                }
                mode4_sz += ctx_streams[k].size();
                if (mode4_sz > mode4_gate_limit || mode4_sz >= best_size) {
                    mode4_aborted = true;
                    break;
                }
            }
        }

        if (!mode4_aborted &&
            nonempty_ctx >= 2 &&
            mode4_sz * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
            if (stats) stats->filter_lo_mode4_candidate_bytes_sum += mode4_sz;
            if (mode4_sz < best_size) {
                best_size = mode4_sz;
                best_mode = 4;
                mode4_streams = ctx_streams;
                mode4_ctx_raw_counts = ctx_raw_counts;
            }
        } else if (nonempty_ctx >= 2) {
            if (stats) stats->filter_lo_mode4_reject_gate++;
        }
        if (stats) stats->filter_lo_mode4_eval_ns += ns_since(t_mode4_eval0, Clock::now());

        #include "lossless_filter_lo_codec_mode7_mode8.inc"
    }

