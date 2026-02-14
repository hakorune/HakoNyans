    static std::vector<uint8_t> decode_plane(
        const uint8_t* td, size_t ts, uint32_t pw, uint32_t ph,
        const uint16_t deq[64], const std::vector<uint8_t>* y_ref = nullptr,
        uint16_t file_version = FileHeader::VERSION
    ) {
        const bool has_band_cdf = file_version >= FileHeader::VERSION_BAND_GROUP_CDF;
        const uint8_t* ptr = td;

        std::vector<Token> dcs;
        std::vector<Token> acs;
        std::vector<Token> ac_low_tokens, ac_mid_tokens, ac_high_tokens;
        std::vector<int8_t> qds;
        std::vector<CfLParams> cfls;
        bool cfl_centered_predictor = false;
        uint32_t block_types_size = 0, palette_size = 0, copy_size = 0;

        const uint8_t* cfl_ptr = nullptr;
        uint32_t sz_cfl = 0;

        if (has_band_cdf) {
            // TileHeader v3 (lossy): 10 fields (40 bytes)
            uint32_t sz[10];
            std::memcpy(sz, td, 40);
            ptr = td + 40;

            dcs = decode_stream(ptr, sz[0]); ptr += sz[0];
            const uint8_t* low_ptr = ptr; ptr += sz[1];
            const uint8_t* mid_ptr = ptr; ptr += sz[2];
            const uint8_t* high_ptr = ptr; ptr += sz[3];
            const uint8_t* pindex_ptr = ptr;
            ptr += sz[4];

            BandPIndexBundle band_pi;
            bool has_band_pindex = false;
            if (sz[4] > 0) {
                has_band_pindex = parse_band_pindex_blob(
                    pindex_ptr, sz[4], sz[1], sz[2], sz[3], band_pi
                );
            }

            auto band_decode_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(3);
            if (band_decode_tokens.acquired()) {
                auto f_low = std::async(std::launch::async, [=, &sz, &band_pi]() {
                    thread_budget::ScopedParallelRegion guard;
                    if (has_band_pindex && band_pi.has_low) {
                        return decode_stream_parallel(low_ptr, sz[1], band_pi.low);
                    }
                    return decode_stream(low_ptr, sz[1]);
                });
                auto f_mid = std::async(std::launch::async, [=, &sz, &band_pi]() {
                    thread_budget::ScopedParallelRegion guard;
                    if (has_band_pindex && band_pi.has_mid) {
                        return decode_stream_parallel(mid_ptr, sz[2], band_pi.mid);
                    }
                    return decode_stream(mid_ptr, sz[2]);
                });
                auto f_high = std::async(std::launch::async, [=, &sz, &band_pi]() {
                    thread_budget::ScopedParallelRegion guard;
                    if (has_band_pindex && band_pi.has_high) {
                        return decode_stream_parallel(high_ptr, sz[3], band_pi.high);
                    }
                    return decode_stream(high_ptr, sz[3]);
                });
                ac_low_tokens = f_low.get();
                ac_mid_tokens = f_mid.get();
                ac_high_tokens = f_high.get();
            } else {
                if (has_band_pindex && band_pi.has_low) ac_low_tokens = decode_stream_parallel(low_ptr, sz[1], band_pi.low);
                else ac_low_tokens = decode_stream(low_ptr, sz[1]);

                if (has_band_pindex && band_pi.has_mid) ac_mid_tokens = decode_stream_parallel(mid_ptr, sz[2], band_pi.mid);
                else ac_mid_tokens = decode_stream(mid_ptr, sz[2]);

                if (has_band_pindex && band_pi.has_high) ac_high_tokens = decode_stream_parallel(high_ptr, sz[3], band_pi.high);
                else ac_high_tokens = decode_stream(high_ptr, sz[3]);
            }

            if (sz[5] > 0) { qds.resize(sz[5]); std::memcpy(qds.data(), ptr, sz[5]); ptr += sz[5]; }
            
            cfl_ptr = ptr;
            sz_cfl = sz[6];
            ptr += sz_cfl;

            block_types_size = sz[7];
            palette_size = sz[8];
            copy_size = sz[9];
        } else {
            // TileHeader v2 (legacy): 8 fields (32 bytes)
            uint32_t sz[8];
            std::memcpy(sz, td, 32);
            ptr = td + 32;

            dcs = decode_stream(ptr, sz[0]); ptr += sz[0];
            if (sz[2] > 0) {
                PIndex pi = PIndexCodec::deserialize(std::span<const uint8_t>(td + 32 + sz[0] + sz[1], sz[2]));
                acs = decode_stream_parallel(ptr, sz[1], pi);
            } else {
                acs = decode_stream(ptr, sz[1]);
            }
            ptr += sz[1] + sz[2];

            if (sz[3] > 0) { qds.resize(sz[3]); std::memcpy(qds.data(), ptr, sz[3]); ptr += sz[3]; }
            
            cfl_ptr = ptr;
            sz_cfl = sz[4];
            ptr += sz_cfl;

            block_types_size = sz[5];
            palette_size = sz[6];
            copy_size = sz[7];
        }

        int nx = pw/8, nb = nx*(ph/8); std::vector<uint8_t> pad(pw*ph);

        parse_cfl_stream(cfl_ptr, sz_cfl, nb, cfls, cfl_centered_predictor);

        std::vector<FileHeader::BlockType> block_types;
        if (block_types_size > 0) {
            block_types = decode_block_types(ptr, block_types_size, nb, file_version);
            ptr += block_types_size;
        } else {
            block_types.assign(nb, FileHeader::BlockType::DCT);
        }
        
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        if (palette_size > 0) {
            int num_palette_blocks = 0;
            for(auto t : block_types) if (t == FileHeader::BlockType::PALETTE) num_palette_blocks++;
            PaletteCodec::decode_palette_stream(ptr, palette_size, palettes, palette_indices, num_palette_blocks);
            ptr += palette_size;
        }

        std::vector<CopyParams> copy_params;
        if (copy_size > 0) {
            int num_copy = 0;
            for(auto t : block_types) if (t == FileHeader::BlockType::COPY) num_copy++;
            CopyCodec::decode_copy_stream(ptr, copy_size, copy_params, num_copy);
            ptr += copy_size;
        }
        
        std::vector<uint32_t> block_starts(nb + 1);
        std::vector<uint32_t> low_starts, mid_starts, high_starts;

        auto build_dct_block_starts = [&](const std::vector<Token>& tokens) {
            std::vector<uint32_t> starts(nb + 1);
            size_t cur = 0;
            for (int i = 0; i < nb; i++) {
                starts[i] = (uint32_t)cur;
                if (block_types[i] == FileHeader::BlockType::DCT) {
                    while (cur < tokens.size()) {
                        if (tokens[cur++].type == TokenType::ZRUN_63) break;
                        if (cur < tokens.size() && (int)tokens[cur - 1].type < 63) cur++; // skip MAGC
                    }
                }
            }
            starts[nb] = (uint32_t)cur;
            return starts;
        };

        if (has_band_cdf) {
            low_starts = build_dct_block_starts(ac_low_tokens);
            mid_starts = build_dct_block_starts(ac_mid_tokens);
            high_starts = build_dct_block_starts(ac_high_tokens);
        } else {
            block_starts = build_dct_block_starts(acs);
        }

        // Threading logic:
        // If Copy Mode is used, we force sequential decoding (nt=1)
        // to ensure Intra-Block Copy vectors point to already-decoded pixels.
        unsigned int nt = thread_budget::max_threads(8);
        nt = std::max(1u, std::min<unsigned int>(nt, (unsigned int)nb));
        thread_budget::ScopedThreadTokens block_decode_tokens;
        if (copy_size == 0 && nt >= 2) {
            block_decode_tokens = thread_budget::ScopedThreadTokens::try_acquire_up_to(nt, 2);
        }
        if (copy_size > 0 || !block_decode_tokens.acquired()) {
            nt = 1;
        } else {
            nt = block_decode_tokens.count();
        }

        auto decode_block_range = [&](int sb, int eb) {
            // Initialize pdc with correct value for the start of this thread's block range
            int16_t pdc = 0;
            int palette_block_idx = 0;
            int copy_block_idx = 0;
            int dct_block_idx = 0;

            // Pre-scan to find correct indices for palette/copy logic
            for (int i = 0; i < sb; i++) {
                 if (block_types[i] == FileHeader::BlockType::DCT) {
                     pdc += Tokenizer::detokenize_dc(dcs[dct_block_idx]);
                     dct_block_idx++;
                 } else if (block_types[i] == FileHeader::BlockType::PALETTE) {
                     palette_block_idx++;
                 } else if (block_types[i] == FileHeader::BlockType::COPY) {
                     copy_block_idx++;
                 }
            }

            int16_t ac[63];
            for (int i = sb; i < eb; i++) {
                int bx = i % nx, by = i / nx;

                if (block_types[i] == FileHeader::BlockType::DCT) {
                    int16_t dc = pdc + Tokenizer::detokenize_dc(dcs[dct_block_idx]); pdc = dc;
                    dct_block_idx++;
                    std::fill(ac, ac + 63, 0);

                    if (has_band_cdf) {
                        size_t low_pos = low_starts[i];
                        size_t mid_pos = mid_starts[i];
                        size_t high_pos = high_starts[i];
                        detokenize_ac_band_block(ac_low_tokens, low_pos, BAND_LOW, ac);
                        detokenize_ac_band_block(ac_mid_tokens, mid_pos, BAND_MID, ac);
                        detokenize_ac_band_block(ac_high_tokens, high_pos, BAND_HIGH, ac);
                    } else {
                        uint32_t start = block_starts[i], end = block_starts[i + 1];
                        int pos = 0;
                        for (uint32_t k = start; k < end && pos < 63; ++k) {
                            const Token& tok = acs[k];
                            if (tok.type == TokenType::ZRUN_63) break;
                            if ((int)tok.type <= 62) {
                                pos += (int)tok.type;
                                if (++k >= end) break;
                                const Token& mt = acs[k];
                                int magc = (int)mt.type - 64;
                                uint16_t sign = (mt.raw_bits >> magc) & 1;
                                uint16_t rem = mt.raw_bits & ((1 << magc) - 1);
                                uint16_t abs_v = (magc > 0) ? ((1 << (magc - 1)) + rem) : 0;
                                if (pos < 63) ac[pos++] = (sign == 0) ? abs_v : -abs_v;
                            }
                        }
                    }
                    float s = 1.0f; if (!qds.empty()) s = 1.0f + qds[i] / 50.0f;
                    int16_t dq[64]; dq[0] = dc * (uint16_t)std::max(1.0f, std::round(deq[0] * s));
                    for (int k = 1; k < 64; k++) dq[k] = ac[k - 1] * (uint16_t)std::max(1.0f, std::round(deq[k] * s));
                    int16_t co[64], bl[64]; Zigzag::inverse_scan(dq, co); DCT::inverse(co, bl);
                    if (y_ref && !cfls.empty() && i < (int)cfls.size()) {
                        if (cfl_centered_predictor) {
                            if (cfls[i].alpha_cr > 0.5f) {
                                int16_t a6 = (int16_t)std::round(cfls[i].alpha_cb * 64.0f);
                                int16_t b = (int16_t)std::round(cfls[i].beta_cb);
                                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                                    int py = (*y_ref)[(by * 8 + y) * pw + (bx * 8 + x)];
                                    int p = (a6 * (py - 128) + 32) >> 6;
                                    p += b;
                                    pad[(by * 8 + y) * pw + (bx * 8 + x)] = std::clamp((int)bl[y * 8 + x] + p, 0, 255);
                                }
                            } else {
                                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) pad[(by * 8 + y) * pw + (bx * 8 + x)] = (uint8_t)std::clamp(bl[y * 8 + x] + 128, 0, 255);
                            }
                        } else {
                            float a = cfls[i].alpha_cb;
                            float b = cfls[i].beta_cb;
                            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                                int py = (*y_ref)[(by * 8 + y) * pw + (bx * 8 + x)];
                                int p = (int)std::lround(a * py + b);
                                pad[(by * 8 + y) * pw + (bx * 8 + x)] = std::clamp((int)bl[y * 8 + x] + p, 0, 255);
                            }
                        }
                    } else {
                        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) pad[(by * 8 + y) * pw + (bx * 8 + x)] = (uint8_t)std::clamp(bl[y * 8 + x] + 128, 0, 255);
                    }
                } else if (block_types[i] == FileHeader::BlockType::PALETTE) {
                    if (palette_block_idx < (int)palettes.size()) {
                        const auto& p = palettes[palette_block_idx];
                        const auto& idx = palette_indices[palette_block_idx];
                        for (int y = 0; y < 8; y++) {
                            for (int x = 0; x < 8; x++) {
                                int k = y * 8 + x;
                                int16_t pal_v = 0;
                                if (k < (int)idx.size()) {
                                    uint8_t pi = idx[k];
                                    if (pi < p.size) pal_v = p.colors[pi];
                                }
                                pad[(by * 8 + y) * pw + (bx * 8 + x)] =
                                    (uint8_t)std::clamp((int)pal_v + 128, 0, 255);
                            }
                        }
                        palette_block_idx++;
                    }
                } else if (block_types[i] == FileHeader::BlockType::COPY) {
                    if (copy_block_idx < (int)copy_params.size()) {
                        CopyParams cp = copy_params[copy_block_idx];
                        for (int y = 0; y < 8; y++) {
                            for (int x = 0; x < 8; x++) {
                                int dst_x = bx * 8 + x;
                                int dst_y = by * 8 + y;
                                int src_x = dst_x + cp.dx;
                                int src_y = dst_y + cp.dy;

                                // Boundary checks
                                // Should we clamp or replicate or black?
                                // Standard clamp
                                src_x = std::clamp(src_x, 0, (int)pw - 1);
                                src_y = std::clamp(src_y, 0, (int)ph - 1);

                                pad[dst_y * pw + dst_x] = pad[src_y * pw + src_x];
                            }
                        }
                        copy_block_idx++;
                    }
                } else {
                     // Unknown block type?
                }
            }
        };

        if (nt >= 2) {
            std::vector<std::future<void>> futs;
            int bpt = nb / (int)nt;
            for (unsigned int t = 0; t < nt; t++) {
                int sb = (int)t * bpt, eb = (t == nt - 1) ? nb : (int)(t + 1) * bpt;
                futs.push_back(std::async(std::launch::async, [&, sb, eb]() {
                    thread_budget::ScopedParallelRegion guard;
                    decode_block_range(sb, eb);
                }));
            }
            for (auto& f : futs) f.get();
        } else {
            decode_block_range(0, nb);
        }
        return pad;
    }
