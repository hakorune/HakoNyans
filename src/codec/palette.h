#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>

namespace hakonyans {

struct Palette {
    uint8_t size;
    uint8_t colors[8];

    Palette() : size(0) { std::memset(colors, 0, 8); }
    
    bool operator==(const Palette& other) const {
        if (size != other.size) return false;
        return std::memcmp(colors, other.colors, size) == 0;
    }
    
    bool operator!=(const Palette& other) const { return !(*this == other); }
};

class PaletteExtractor {
public:
    static Palette extract(const int16_t* block, int max_colors = 8) {
        std::map<int16_t, int> counts;
        for (int i = 0; i < 64; i++) {
            counts[block[i]]++;
        }
        
        if (counts.size() > (size_t)max_colors) {
            // Too many colors, return empty palette to signal failure (fallback to DCT)
            // Or implement simple clustering (e.g. k-means) here if we want lossy palette
            // For now, strict lossless palette extraction
            return Palette(); 
        }

        Palette p;
        p.size = (uint8_t)counts.size();
        int idx = 0;
        // Sort by frequency (descending) for better compression?
        // Or sort by value for better delta coding?
        // Let's sort by frequency for now.
        std::vector<std::pair<int16_t, int>> sorted_counts(counts.begin(), counts.end());
        std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        for (const auto& kv : sorted_counts) {
            // colors stored as uint8_t (0-255), input block is -128..127
            p.colors[idx++] = (uint8_t)(kv.first + 128);
        }
        return p;
    }
    
    static std::vector<uint8_t> map_indices(const int16_t* block, const Palette& p) {
        std::vector<uint8_t> indices(64);
        for (int i = 0; i < 64; i++) {
            uint8_t val = (uint8_t)(block[i] + 128);
            int best_idx = 0;
            int min_dist = 256*256;
            
            for (int k = 0; k < p.size; k++) {
                if (p.colors[k] == val) {
                    best_idx = k;
                    break;
                }
                // Fallback for lossy (if we implement clustering later)
                int dist = std::abs((int)p.colors[k] - (int)val);
                if (dist < min_dist) {
                    min_dist = dist;
                    best_idx = k;
                }
            }
            indices[i] = (uint8_t)best_idx;
        }
        return indices;
    }
};

class PaletteCodec {
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
                    bits_in_accum += 8; // Virtual zeros
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
    static std::vector<uint8_t> encode_palette_stream(
        const std::vector<Palette>& palettes,
        const std::vector<std::vector<uint8_t>>& indices_list
    ) {
        std::vector<uint8_t> out;
        Palette prev_pal;
        
        for (size_t i = 0; i < palettes.size(); i++) {
            const Palette& p = palettes[i];
            const auto& idx = indices_list[i];
            
            bool use_prev = (p == prev_pal && p.size > 0);
            uint8_t head = (use_prev ? 0x80 : 0) | ((p.size - 1) & 0x07);
            out.push_back(head);
            
            if (!use_prev) {
                for (int k = 0; k < p.size; k++) {
                    out.push_back(p.colors[k]);
                }
                prev_pal = p;
            }
            
            int bits = 1;
            if (p.size > 4) bits = 3;
            else if (p.size > 2) bits = 2;
            
            BitWriter bw;
            for (uint8_t v : idx) {
                bw.write(v, bits);
            }
            auto packed = bw.flush();
            out.insert(out.end(), packed.begin(), packed.end());
        }
        return out;
    }
    
    static void decode_palette_stream(
        const uint8_t* data, size_t size, 
        std::vector<Palette>& out_palettes,
        std::vector<std::vector<uint8_t>>& out_indices_list,
        int num_blocks
    ) {
        size_t pos = 0;
        Palette prev_pal;
        
        for (int i = 0; i < num_blocks; i++) {
            if (pos >= size) break;
            
            uint8_t head = data[pos++];
            bool use_prev = (head & 0x80);
            int p_size = (head & 0x07) + 1;
            
            Palette p;
            p.size = p_size;
            
            if (use_prev) {
                p = prev_pal;
            } else {
                for (int k = 0; k < p_size; k++) {
                    if (pos < size) p.colors[k] = data[pos++];
                }
                prev_pal = p;
            }
            out_palettes.push_back(p);
            
            int bits = 1;
            if (p.size > 4) bits = 3;
            else if (p.size > 2) bits = 2;
            
            BitReader br(data + pos, size - pos);
            std::vector<uint8_t> idx(64);
            for (int k = 0; k < 64; k++) {
                idx[k] = (uint8_t)br.read(bits);
            }
            out_indices_list.push_back(idx);
            
            pos += br.bytes_consumed();
        }
    }
};

} // namespace hakonyans
