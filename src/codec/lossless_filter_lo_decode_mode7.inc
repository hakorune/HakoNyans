            if (stats) stats->plane_filter_lo_mode7_count++;
            const uint32_t shared_mask = (uint32_t)payload[0]
                                       | ((uint32_t)payload[1] << 8)
                                       | ((uint32_t)payload[2] << 16)
                                       | ((uint32_t)payload[3] << 24);
            uint32_t lens[6] = {0, 0, 0, 0, 0, 0};
            for (int k = 0; k < 6; k++) {
                const size_t pos = 4 + (size_t)k * 4;
                lens[k] = (uint32_t)payload[pos]
                        | ((uint32_t)payload[pos + 1] << 8)
                        | ((uint32_t)payload[pos + 2] << 16)
                        | ((uint32_t)payload[pos + 3] << 24);
            }
            size_t off = 28;
            bool lens_ok = true;
            for (int k = 0; k < 6; k++) {
                if (lens[k] > payload_size - off) {
                    lens_ok = false;
                    break;
                }
                off += lens[k];
            }

            if (!lens_ok) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                const auto t_mode4_rows0 = Clock::now();
                std::vector<int> row_lens(pad_h, 0);
                std::vector<uint32_t> ctx_expected(6, 0);
                for (uint32_t y = 0; y < pad_h; y++) {
                    int count = 0;
                    int row_idx = y / 8;
                    for (int bx = 0; bx < nx; bx++) {
                        if (block_types[row_idx * nx + bx] == FileHeader::BlockType::DCT) {
                            count += 8;
                        }
                    }
                    row_lens[y] = count;
                    if (count > 0) {
                        uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
                        if (fid > 5) fid = 0;
                        ctx_expected[fid] += (uint32_t)count;
                    }
                }
                const auto t_mode4_rows1 = Clock::now();
                add_ns(stats ? &stats->plane_filter_lo_mode4_row_lens_ns : nullptr, t_mode4_rows0, t_mode4_rows1);

                std::vector<std::vector<uint8_t>> ctx_decoded(6);
                size_t ctx_offsets[6] = {0, 0, 0, 0, 0, 0};
                off = 28;
                for (int k = 0; k < 6; k++) {
                    ctx_offsets[k] = off;
                    off += lens[k];
                }

                uint64_t shared_ctx_used = 0;
                for (int k = 0; k < 6; k++) {
                    if (ctx_expected[k] == 0 || lens[k] == 0) continue;
                    if ((shared_mask >> k) & 1u) shared_ctx_used++;
                }
                if (stats) stats->plane_filter_lo_mode7_shared_ctx_sum += shared_ctx_used;

                const unsigned int hw_threads = thread_budget::max_threads();
                thread_budget::ScopedThreadTokens ctx_parallel_tokens;
                if (hw_threads >= 6 && raw_count >= 8192) {
                    ctx_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(6);
                }
                const bool allow_parallel_ctx = ctx_parallel_tokens.acquired();
                if (stats) {
                    if (allow_parallel_ctx) stats->plane_filter_lo_mode4_parallel_ctx_tiles++;
                    else stats->plane_filter_lo_mode4_sequential_ctx_tiles++;
                }

                if (allow_parallel_ctx) {
                    struct CtxDecodeResult {
                        std::vector<uint8_t> bytes;
                        uint64_t elapsed_ns = 0;
                        bool used_shared = false;
                    };
                    std::vector<std::future<CtxDecodeResult>> futs(6);
                    std::vector<bool> launched(6, false);
                    for (int k = 0; k < 6; k++) {
                        if (lens[k] == 0) continue;
                        launched[k] = true;
                        const bool use_shared_ctx = ((shared_mask >> k) & 1u) != 0u;
                        futs[k] = std::async(std::launch::async, [&, k, use_shared_ctx]() {
                            thread_budget::ScopedParallelRegion guard;
                            const auto t_ctx0 = Clock::now();
                            std::vector<uint8_t> out;
                            if (use_shared_ctx) {
                                out = decode_byte_stream_shared_lz(
                                    payload + ctx_offsets[k], lens[k], ctx_expected[k]
                                );
                            } else {
                                out = decode_byte_stream(
                                    payload + ctx_offsets[k], lens[k], ctx_expected[k]
                                );
                            }
                            const auto t_ctx1 = Clock::now();
                            CtxDecodeResult r;
                            r.bytes = std::move(out);
                            r.elapsed_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                t_ctx1 - t_ctx0
                            ).count();
                            r.used_shared = use_shared_ctx;
                            return r;
                        });
                    }
                    for (int k = 0; k < 6; k++) {
                        if (!launched[k]) continue;
                        auto r = futs[k].get();
                        ctx_decoded[k] = std::move(r.bytes);
                        if (stats) {
                            if (r.used_shared) stats->plane_filter_lo_decode_shared_rans_ns += r.elapsed_ns;
                            else stats->plane_filter_lo_decode_rans_ns += r.elapsed_ns;
                        }
                    }
                } else {
                    for (int k = 0; k < 6; k++) {
                        if (lens[k] == 0) continue;
                        const bool use_shared_ctx = ((shared_mask >> k) & 1u) != 0u;
                        if (use_shared_ctx) {
                            ctx_decoded[k] = timed_decode_shared_rans(
                                payload + ctx_offsets[k], lens[k], ctx_expected[k]
                            );
                        } else {
                            ctx_decoded[k] = timed_decode_rans(
                                payload + ctx_offsets[k], lens[k], ctx_expected[k]
                            );
                        }
                    }
                }

                std::vector<size_t> ctx_pos(6, 0);
                lo_bytes.assign(raw_count, 0);
                uint8_t* lo_ptr = lo_bytes.data();
                size_t out_idx = 0;
                for (uint32_t y = 0; y < pad_h && out_idx < raw_count; y++) {
                    int len = row_lens[y];
                    if (len <= 0) continue;
                    uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
                    if (fid > 5) fid = 0;
                    size_t take = std::min<size_t>((size_t)len, raw_count - out_idx);
                    const auto& ctx = ctx_decoded[fid];
                    size_t pos = ctx_pos[fid];
                    size_t available = (pos < ctx.size()) ? (ctx.size() - pos) : 0;
                    size_t copy_n = std::min(take, available);
                    if (copy_n > 0) {
                        std::memcpy(lo_ptr + out_idx, ctx.data() + pos, copy_n);
                        ctx_pos[fid] += copy_n;
                    }
                    out_idx += take;
                }
            }
