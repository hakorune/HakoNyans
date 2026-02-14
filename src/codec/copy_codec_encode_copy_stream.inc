        std::vector<uint8_t> out;
        if (params.empty()) return out;

        // Prefer compact modes when all vectors are in the small table.
        bool use_small = true;
        uint8_t used_mask = 0;
        for (const auto& p : params) {
            int si = small_vector_index(p);
            if (si < 0) {
                use_small = false;
                break;
            }
            used_mask |= (uint8_t)(1u << si);
        }

        if (use_small) {
            int used_count = popcount4(used_mask);
            int bits_dyn = small_vector_bits(used_count);

            // Build codebook (shared by mode 2 and mode 3)
            uint8_t small_to_code[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            uint8_t code_to_small[4] = {0, 0, 0, 0};
            uint8_t code = 0;
            for (uint8_t si = 0; si < 4; si++) {
                if ((used_mask >> si) & 1u) {
                    small_to_code[si] = code;
                    code_to_small[code] = si;
                    code++;
                }
            }

            // Mode 1 (legacy): [mode=1][2-bit codes...]
            size_t mode1_size = 1 + ((params.size() * 2 + 7) >> 3);
            // Mode 2 (dynamic): [mode=2][used_mask][N-bit codes...]
            size_t mode2_size = 2 + ((params.size() * bits_dyn + 7) >> 3);

            // Mode 3 (RLE token): [mode=3][used_mask][run_tokens...]
            // token: bit7..6=symbol_code(0..3), bit5..0=run_minus1(0..63)
            size_t mode3_tokens = 0;
            {
                size_t i = 0;
                size_t n = params.size();
                while (i < n) {
                    int si = small_vector_index(params[i]);
                    uint8_t sc = small_to_code[si < 0 ? 0 : si];
                    size_t run = 1;
                    while (i + run < n && run < 64) {
                        int si2 = small_vector_index(params[i + run]);
                        uint8_t sc2 = small_to_code[si2 < 0 ? 0 : si2];
                        if (sc2 != sc) break;
                        run++;
                    }
                    mode3_tokens++;
                    i += run;
                }
            }
            size_t mode3_size = 2 + mode3_tokens;

            // Pick smallest mode
            size_t best_size = mode1_size;
            int best_mode = 1;
            if (mode2_size <= best_size) { best_size = mode2_size; best_mode = 2; }
            if (mode3_size < best_size)  { best_size = mode3_size; best_mode = 3; }

            if (best_mode == 3) {
                out.push_back(3);  // mode=3 (RLE token)
                out.push_back(used_mask);

                size_t i = 0;
                size_t n = params.size();
                while (i < n) {
                    int si = small_vector_index(params[i]);
                    uint8_t sc = small_to_code[si < 0 ? 0 : si];
                    size_t run = 1;
                    while (i + run < n && run < 64) {
                        int si2 = small_vector_index(params[i + run]);
                        uint8_t sc2 = small_to_code[si2 < 0 ? 0 : si2];
                        if (sc2 != sc) break;
                        run++;
                    }
                    // token: bit7..6=symbol_code, bit5..0=run_minus1
                    out.push_back((uint8_t)((sc << 6) | ((run - 1) & 0x3F)));
                    i += run;
                }
                return out;
            }

            if (best_mode == 2) {
                out.push_back(2);  // mode=2 (dynamic small-vector codebook)
                out.push_back(used_mask);

                if (bits_dyn == 0) return out;

                BitWriter bw;
                for (const auto& p : params) {
                    int si = small_vector_index(p);
                    uint8_t sc = small_to_code[si < 0 ? 0 : si];
                    bw.write((uint32_t)sc, bits_dyn);
                }
                auto packed = bw.flush();
                out.insert(out.end(), packed.begin(), packed.end());
                return out;
            }

            // mode=1 kept for backward compatibility and for tie-break cases.
            out.push_back(1);
            BitWriter bw;
            for (const auto& p : params) {
                bw.write((uint32_t)small_vector_index(p), 2);
            }
            auto packed = bw.flush();
            out.insert(out.end(), packed.begin(), packed.end());
            return out;
        }

        // Mode 0: raw 16-bit dx/dy pairs (legacy-compatible payload).
        out.push_back(0); // mode=0
        for (const auto& p : params) {
            uint16_t ux = (uint16_t)p.dx;
            out.push_back(ux & 0xFF);
            out.push_back((ux >> 8) & 0xFF);

            uint16_t uy = (uint16_t)p.dy;
            out.push_back(uy & 0xFF);
            out.push_back((uy >> 8) & 0xFF);
        }
        return out;
