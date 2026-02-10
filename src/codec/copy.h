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
    static std::vector<uint8_t> encode_copy_stream(const std::vector<CopyParams>& params) {
        std::vector<uint8_t> out;
        if (params.empty()) return out;

        // Simple predictive coding?
        // diff = curr - prev
        // For now, raw encoding or simple delta.
        // Screen Profile Guide Step 3 doesn't specify heavy entropy coding yet.
        // Let's use simple variable length or just raw 16-bit for simplicity and update later if needed.
        // Actually, let's use a simple bit-packing:
        // Most offsets are small.
        // But for "Full Implementation", let's separate fields:
        // One stream for DX, one for DY?
        // For now: interleaved raw 16-bit (Little Endian)
        
        for (const auto& p : params) {
            // Write dx (16-bit)
            uint16_t ux = (uint16_t)p.dx;
            out.push_back(ux & 0xFF);
            out.push_back((ux >> 8) & 0xFF);
            
            // Write dy (16-bit)
            uint16_t uy = (uint16_t)p.dy;
            out.push_back(uy & 0xFF);
            out.push_back((uy >> 8) & 0xFF);
        }
        return out;
    }

    static void decode_copy_stream(const uint8_t* data, size_t size, std::vector<CopyParams>& out_params, int num_blocks) {
        size_t pos = 0;
        for(int i=0; i<num_blocks; i++) {
            if (pos + 4 > size) break;
            
            int16_t dx = (int16_t)(data[pos] | (data[pos+1] << 8));
            pos += 2;
            int16_t dy = (int16_t)(data[pos] | (data[pos+1] << 8));
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
