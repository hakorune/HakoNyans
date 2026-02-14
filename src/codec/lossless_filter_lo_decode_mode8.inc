            if (stats) stats->plane_filter_lo_mode8_count++;

            // Read ctx_codec_id[6] from payload[0..5]
            uint8_t codec_ids[6];
            for (int k = 0; k < 6; k++) {
                codec_ids[k] = payload[k];
            }

            // Read lens[6] from payload[6..29]
            uint32_t lens[6] = {0, 0, 0, 0, 0, 0};
            for (int k = 0; k < 6; k++) {
                const size_t pos = 6 + (size_t)k * 4;
                lens[k] = (uint32_t)payload[pos]
                        | ((uint32_t)payload[pos + 1] << 8)
                        | ((uint32_t)payload[pos + 2] << 16)
                        | ((uint32_t)payload[pos + 3] << 24);
            }

            // Validate payload_size == 30 + sum(lens)
            size_t total_stream_size = 30;
            for (int k = 0; k < 6; k++) {
                total_stream_size += lens[k];
            }

            // Validate codec_ids and lens
            bool validation_ok = (payload_size == total_stream_size);
            for (int k = 0; k < 6 && validation_ok; k++) {
                if (codec_ids[k] != 0 && codec_ids[k] != 1 && codec_ids[k] != 2 && codec_ids[k] != 255) {
                    validation_ok = false;  // Invalid codec_id
                }
                if (codec_ids[k] == 255 && lens[k] != 0) {
                    validation_ok = false;  // Empty ctx must have len=0
                }
            }

            if (!validation_ok) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                const auto t_mode8_rows0 = Clock::now();
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
                const auto t_mode8_rows1 = Clock::now();
                add_ns(stats ? &stats->plane_filter_lo_mode4_row_lens_ns : nullptr, t_mode8_rows0, t_mode8_rows1);

                std::vector<std::vector<uint8_t>> ctx_decoded(6);
                size_t ctx_offsets[6] = {0, 0, 0, 0, 0, 0};
                size_t off = 30;
                for (int k = 0; k < 6; k++) {
                    ctx_offsets[k] = off;
                    off += lens[k];
                }

                // Decode each context based on codec_id
                bool decode_ok = true;
                for (int k = 0; k < 6 && decode_ok; k++) {
                    if (ctx_expected[k] == 0 || lens[k] == 0) continue;

                    std::vector<uint8_t> decoded;
                    if (codec_ids[k] == 0) {
                        // Legacy rANS
                        decoded = timed_decode_rans(payload + ctx_offsets[k], lens[k], ctx_expected[k]);
                    } else if (codec_ids[k] == 1) {
                        // Delta + rANS: decode then cumulative sum
                        auto delta = timed_decode_rans(payload + ctx_offsets[k], lens[k], ctx_expected[k]);
                        decoded.resize(delta.size());
                        if (!delta.empty()) {
                            decoded[0] = delta[0];
                            for (size_t i = 1; i < delta.size(); i++) {
                                decoded[i] = (uint8_t)(decoded[i - 1] + delta[i]);
                            }
                        }
                    } else if (codec_ids[k] == 2) {
                        // LZ + rANS(shared): decode shared then decompress
                        auto lz_payload = timed_decode_shared_rans(payload + ctx_offsets[k], lens[k], 0);
                        if (!lz_payload.empty()) {
                            decoded = timed_decompress_lz(lz_payload.data(), lz_payload.size(), ctx_expected[k]);
                        }
                    }

                    // Validate decoded size
                    if (decoded.size() != ctx_expected[k]) {
                        decode_ok = false;
                    } else {
                        ctx_decoded[k] = std::move(decoded);
                    }
                }

                if (!decode_ok) {
                    lo_bytes.assign(raw_count, 0);
                    if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                } else {
                    // Reassemble rows
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
            }
