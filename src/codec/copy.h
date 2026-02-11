#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace hakonyans {

struct CopyParams {
    int16_t dx; // Pixel offset X
    int16_t dy; // Pixel offset Y

    CopyParams() : dx(0), dy(0) {}
    CopyParams(int16_t x, int16_t y) : dx(x), dy(y) {}
    
    bool operator==(const CopyParams& other) const {
        return dx == other.dx && dy == other.dy;
    }
};

class CopyCodec {
    class BitWriter {
        std::vector<uint8_t> buffer;
        uint64_t accum = 0;
        int bits_acc = 0;
    public:
        void write(uint32_t val, int bits) {
            accum |= ((uint64_t)val << bits_acc);
            bits_acc += bits;
            while (bits_acc >= 8) {
                buffer.push_back((uint8_t)(accum & 0xFF));
                accum >>= 8;
                bits_acc -= 8;
            }
        }
        
        std::vector<uint8_t> flush() {
            if (bits_acc > 0) {
                buffer.push_back((uint8_t)(accum & 0xFF));
            }
            return buffer;
        }
    };
    
    class BitReader {
        const uint8_t* ptr;
        size_t size;
        uint64_t accum = 0;
        int bits_in_accum = 0;
        size_t byte_pos = 0;
        
    public:
        BitReader(const uint8_t* p, size_t s) : ptr(p), size(s) {}
        
        uint32_t read(int bits) {
            while (bits_in_accum < bits) {
                if (byte_pos < size) {
                    accum |= ((uint64_t)ptr[byte_pos++] << bits_in_accum);
                    bits_in_accum += 8;
                } else {
                    bits_in_accum += 8;
                }
            }
            uint32_t ret = (uint32_t)(accum & ((1ULL << bits) - 1));
            accum >>= bits;
            bits_in_accum -= bits;
            return ret;
        }

        size_t bytes_consumed() const {
            return byte_pos;
        }
    };

public:
    static int small_vector_bits(int symbol_count) {
        if (symbol_count <= 1) return 0;
        if (symbol_count <= 2) return 1;
        return 2;
    }

    static int popcount4(uint8_t v) {
        int c = 0;
        for (int i = 0; i < 4; i++) c += (v >> i) & 1;
        return c;
    }

    static CopyParams small_vector_from_index(uint32_t idx) {
        switch (idx) {
            case 0: return CopyParams(-8, 0);   // left
            case 1: return CopyParams(0, -8);   // up
            case 2: return CopyParams(-8, -8);  // up-left
            case 3: return CopyParams(8, -8);   // up-right
            default: return CopyParams(0, 0);
        }
    }

    static int small_vector_index(const CopyParams& p) {
        if (p.dx == -8 && p.dy == 0) return 0;
        if (p.dx == 0 && p.dy == -8) return 1;
        if (p.dx == -8 && p.dy == -8) return 2;
        if (p.dx == 8 && p.dy == -8) return 3;
        return -1;
    }

    static std::vector<uint8_t> encode_copy_stream(const std::vector<CopyParams>& params) {
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
    }

    static void decode_copy_stream(const uint8_t* data, size_t size, std::vector<CopyParams>& out_params, int num_blocks) {
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
    }
};

class IntraBCSearch {
public:
    // Search for best match of 8x8 block in the already encoded area.
    // full_img: pointer to full image data (reconstructed or original)
    // stride: image width/stride
    // bx, by: current block position (in units of 8x8 blocks)
    // search_radius: max pixels to search back
    // out_params: result dx, dy
    // Returns: SAD (Sum of Absolute Differences) or MSE
    static int search(const uint8_t* full_img, uint32_t stride, uint32_t height, int bx, int by, int search_radius, CopyParams& out_params) {
        int best_sad = 255 * 64 * 64; // Max possible SAD
        out_params.dx = 0;
        out_params.dy = 0;
        
        // Block position in pixels
        int cur_x = bx * 8;
        int cur_y = by * 8;
        
        // Extract current block for faster comparison
        uint8_t cur_block[64];
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                cur_block[y*8+x] = full_img[(cur_y + y) * stride + (cur_x + x)];
            }
        }
        
        // Valid search area:
        // We can copy from any pixel (rx, ry) such that:
        // 1. (rx, ry) is within image bounds (0..width-1, 0..height-1)
        // 2. The 8x8 block at (rx, ry) is "already decoded".
        //    In sequential decode, this means (ry < cur_y) OR (ry == cur_y && rx < cur_x).
        //    Actually, we need the whole 8x8 block to be available. 
        //    So (ry + 7 < cur_y) is strictly safe? 
        //    No, usually we define "available" by top-left corner causality + Delay?
        //    For standard IntraBC: 
        //    The reference block vectors must point to pixels that have been reconstructed.
        //    Since we decode block-by-block, if we copy from (rx, ry), we need pixels at (rx..rx+7, ry..ry+7) to be ready.
        //    If we limit search to "Completed Blocks", i.e. Reference Block Index < Current Block Index.
        //    Let's use a simpler constraint:
        //    Search center (rx, ry) within [cur_x - radius, cur_x + radius] x [cur_y - radius, cur_y].
        //    AND check if the block index of (rx, ry) is < current.
        //    Actually, simple "Left and Top" search is enough for text.
        
        // Search loops
        // Optimization: Spiral search? Or just raster scan in window.
        // Search window: 
        //   y: [cur_y - search_radius, cur_y]
        //   x: [cur_x - search_radius, cur_x + search_radius] (but limited by y)
        
        int start_y = std::max(0, cur_y - search_radius);
        int end_y = cur_y; // Can go up to cur_y? 
                           // If ry == cur_y, rx must be < cur_x - some_delay?
                           // Let's assume we can copy from strictly left pixels?
                           // rx <= cur_x - 8 is safe (previous block).
        
        // For simplicity and safety in V1:
        // Only search completely "past" blocks.
        // Top area: y <= cur_y - 8
        // Left area: y == cur_y, x <= cur_x - 8
        
        // 1. Scan Top area
        for (int ry = std::max(0, cur_y - search_radius); ry <= cur_y - 8; ry++) {
             for (int rx = std::max(0, cur_x - search_radius); rx < std::min((int)stride - 8, cur_x + search_radius); rx++) {
                 int sad = calc_sad(cur_block, full_img, stride, rx, ry);
                 if (sad < best_sad) {
                     best_sad = sad;
                     out_params.dx = (int16_t)(rx - cur_x);
                     out_params.dy = (int16_t)(ry - cur_y);
                     if (sad == 0) return 0; // Perfect match found
                 }
             }
        }
        
        // 2. Scan Left area (Same row)
        // Only scan if left is available
        int ry = cur_y;
        for (int rx = std::max(0, cur_x - search_radius); rx <= cur_x - 8; rx++) {
             int sad = calc_sad(cur_block, full_img, stride, rx, ry);
             if (sad < best_sad) {
                 best_sad = sad;
                 out_params.dx = (int16_t)(rx - cur_x);
                 out_params.dy = (int16_t)(ry - cur_y);
                 if (sad == 0) return 0;
             }
        }
        
        return best_sad;
    }

private:
    static int calc_sad(const uint8_t* cur, const uint8_t* img, uint32_t stride, int rx, int ry) {
        int sad = 0;
        for (int y = 0; y < 8; y++) {
            const uint8_t* row = img + (ry + y) * stride + rx;
            for (int x = 0; x < 8; x++) {
                sad += std::abs((int)cur[y*8+x] - (int)row[x]);
            }
        }
        return sad;
    }
};

} // namespace hakonyans
