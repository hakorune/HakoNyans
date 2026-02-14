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
        #include "copy_codec_encode_copy_stream.inc"
    }

    static void decode_copy_stream(const uint8_t* data, size_t size, std::vector<CopyParams>& out_params, int num_blocks) {
        #include "copy_codec_decode_copy_stream.inc"
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
        #include "copy_intra_bc_search.inc"
    }

private:
    static int calc_sad(const uint8_t* cur, const uint8_t* img, uint32_t stride, int rx, int ry) {
        #include "copy_intra_bc_calc_sad.inc"
    }
};

} // namespace hakonyans
