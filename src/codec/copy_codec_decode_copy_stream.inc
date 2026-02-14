        if (size == 0 || num_blocks <= 0) return;

        size_t pos = 0;
        uint8_t mode = 0;

        // Backward compatibility:
        // Old streams had no mode byte and were exactly 4*num_blocks bytes.
        if (size == (size_t)num_blocks * 4) {
            mode = 0;
        } else {
            mode = data[0];
            pos = 1;
        }

        if (mode == 3) {
            if (pos >= size) return;
            uint8_t used_mask = data[pos++];

            uint8_t code_to_small[4] = {0, 0, 0, 0};
            int used_count = 0;
            for (uint8_t si = 0; si < 4; si++) {
                if ((used_mask >> si) & 1u) {
                    code_to_small[used_count++] = si;
                }
            }
            if (used_count <= 0) return;

            // Expand run tokens
            int emitted = 0;
            while (pos < size && emitted < num_blocks) {
                uint8_t token = data[pos++];
                uint8_t sc = (token >> 6) & 0x03;
                int run = (token & 0x3F) + 1;

                if ((int)sc >= used_count) sc = 0; // fail-safe
                CopyParams p = small_vector_from_index(code_to_small[sc]);
                int to_emit = std::min(run, num_blocks - emitted);
                for (int k = 0; k < to_emit; k++) {
                    out_params.push_back(p);
                }
                emitted += to_emit;
            }
            // Pad remaining if stream was truncated
            while (emitted < num_blocks) {
                out_params.push_back(small_vector_from_index(0));
                emitted++;
            }
            return;
        }

        if (mode == 2) {
            if (pos >= size) return;
            uint8_t used_mask = data[pos++];

            uint8_t code_to_small[4] = {0, 0, 0, 0};
            int used_count = 0;
            for (uint8_t si = 0; si < 4; si++) {
                if ((used_mask >> si) & 1u) {
                    code_to_small[used_count++] = si;
                }
            }
            if (used_count <= 0) return;

            int bits_dyn = small_vector_bits(used_count);
            if (bits_dyn == 0) {
                CopyParams p = small_vector_from_index(code_to_small[0]);
                out_params.insert(out_params.end(), num_blocks, p);
                return;
            }

            BitReader br(data + pos, size - pos);
            for (int i = 0; i < num_blocks; i++) {
                uint32_t code = br.read(bits_dyn);
                if ((int)code >= used_count) code = 0;
                out_params.push_back(small_vector_from_index(code_to_small[code]));
            }
            return;
        }

        if (mode == 1) {
            BitReader br(data + pos, size - pos);
            for (int i = 0; i < num_blocks; i++) {
                uint32_t idx = br.read(2);
                out_params.push_back(small_vector_from_index(idx));
            }
            return;
        }

        for (int i = 0; i < num_blocks; i++) {
            if (pos + 4 > size) break;

            int16_t dx = (int16_t)(data[pos] | (data[pos + 1] << 8));
            pos += 2;
            int16_t dy = (int16_t)(data[pos] | (data[pos + 1] << 8));
            pos += 2;

            out_params.emplace_back(dx, dy);
        }
