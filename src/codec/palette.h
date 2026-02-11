#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>
#include <unordered_map>

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

    static constexpr uint8_t kStreamV2Magic = 0x40;

    static uint64_t indices_to_mask64(const std::vector<uint8_t>& idx) {
        uint64_t mask = 0;
        size_t n = std::min<size_t>(64, idx.size());
        for (size_t i = 0; i < n; i++) {
            if (idx[i] & 1u) mask |= (1ULL << i);
        }
        return mask;
    }

    static std::vector<uint8_t> mask64_to_indices(uint64_t mask) {
        std::vector<uint8_t> idx(64, 0);
        for (int i = 0; i < 64; i++) {
            idx[i] = (uint8_t)((mask >> i) & 1u);
        }
        return idx;
    }

    static int bits_for_palette_size(int p_size) {
        if (p_size <= 1) return 0;
        if (p_size <= 2) return 1;
        if (p_size <= 4) return 2;
        return 3;
    }

public:
    static std::vector<uint8_t> encode_palette_stream(
        const std::vector<Palette>& palettes,
        const std::vector<std::vector<uint8_t>>& indices_list
    ) {
        std::vector<uint8_t> out;
        if (palettes.empty()) return out;

        // v2 format:
        // [magic=0x40][flags]
        //   if flags&1: [dict_count:u8][dict masks: dict_count * 8 bytes]
        // Then per block:
        //   [head][palette colors?][indices payload]
        // indices payload:
        //   size=1 : omitted
        //   size=2 : [dict_index:u8] if flags&1 else [mask64:8B]
        //   size>2 : legacy bit-packed 64 indices
        uint8_t flags = 0;
        std::vector<uint64_t> mask_dict;
        std::unordered_map<uint64_t, uint8_t> mask_to_id;

        bool dict_overflow = false;
        int two_color_blocks = 0;
        for (size_t i = 0; i < palettes.size() && i < indices_list.size(); i++) {
            const Palette& p = palettes[i];
            if (p.size != 2) continue;
            two_color_blocks++;
            uint64_t mask = indices_to_mask64(indices_list[i]);
            if (mask_to_id.find(mask) == mask_to_id.end()) {
                if (mask_dict.size() < 255) {
                    uint8_t id = (uint8_t)mask_dict.size();
                    mask_to_id[mask] = id;
                    mask_dict.push_back(mask);
                } else {
                    dict_overflow = true;
                }
            }
        }

        // Enable dictionary when it is materially smaller than raw 8B per size-2 block.
        // AND we can represent all masks (no overflow).
        if (two_color_blocks > 0 && !mask_dict.empty() && !dict_overflow) {
            size_t raw_size = (size_t)two_color_blocks * 8;
            size_t dict_size = 1 + mask_dict.size() * 8 + (size_t)two_color_blocks; // count + dict + 1B refs
            if (dict_size < raw_size) flags |= 0x01;
        }

        out.push_back(kStreamV2Magic);
        out.push_back(flags);
        if (flags & 0x01) {
            out.push_back((uint8_t)mask_dict.size());
            for (uint64_t mask : mask_dict) {
                for (int b = 0; b < 8; b++) {
                    out.push_back((uint8_t)((mask >> (8 * b)) & 0xFF));
                }
            }
        }

        Palette prev_pal;
        for (size_t i = 0; i < palettes.size(); i++) {
            const Palette& p = palettes[i];
            const auto& idx = indices_list[i];

            bool use_prev = (p == prev_pal && p.size > 0);
            uint8_t head = (use_prev ? 0x80 : 0) | ((p.size - 1) & 0x07);
            out.push_back(head);

            if (!use_prev) {
                for (int k = 0; k < p.size; k++) out.push_back(p.colors[k]);
                prev_pal = p;
            }

            if (p.size <= 1) {
                // Solid-color palette block: indices are implicitly all zero.
                continue;
            }

            if (p.size == 2) {
                uint64_t mask = indices_to_mask64(idx);
                if (flags & 0x01) {
                    auto it = mask_to_id.find(mask);
                    out.push_back((it != mask_to_id.end()) ? it->second : 0);
                } else {
                    for (int b = 0; b < 8; b++) {
                        out.push_back((uint8_t)((mask >> (8 * b)) & 0xFF));
                    }
                }
                continue;
            }

            int bits = bits_for_palette_size(p.size);
            BitWriter bw;
            for (uint8_t v : idx) bw.write(v, bits);
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
        if (size == 0 || num_blocks <= 0) return;

        size_t pos = 0;
        bool is_v2 = false;
        uint8_t flags = 0;
        std::vector<uint64_t> mask_dict;

        if (data[0] == kStreamV2Magic) {
            is_v2 = true;
            pos = 1;
            if (pos < size) flags = data[pos++];

            if (flags & 0x01) {
                if (pos >= size) return;
                uint8_t dict_count = data[pos++];
                mask_dict.reserve(dict_count);
                for (uint8_t i = 0; i < dict_count; i++) {
                    if (pos + 8 > size) return;
                    uint64_t mask = 0;
                    for (int b = 0; b < 8; b++) {
                        mask |= ((uint64_t)data[pos++] << (8 * b));
                    }
                    mask_dict.push_back(mask);
                }
            }
        }

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

            if (is_v2 && p.size <= 1) {
                out_indices_list.push_back(std::vector<uint8_t>(64, 0));
                continue;
            }

            if (is_v2 && p.size == 2) {
                uint64_t mask = 0;
                if (flags & 0x01) {
                    uint8_t idx_id = (pos < size) ? data[pos++] : 0;
                    if (idx_id < mask_dict.size()) mask = mask_dict[idx_id];
                } else {
                    if (pos + 8 > size) break;
                    for (int b = 0; b < 8; b++) {
                        mask |= ((uint64_t)data[pos++] << (8 * b));
                    }
                }
                out_indices_list.push_back(mask64_to_indices(mask));
                continue;
            }

            int bits = bits_for_palette_size(p.size);
            BitReader br(data + pos, size - pos);
            std::vector<uint8_t> idx(64, 0);
            for (int k = 0; k < 64; k++) idx[k] = (uint8_t)br.read(bits);
            out_indices_list.push_back(idx);
            pos += br.bytes_consumed();
        }
    }
};

} // namespace hakonyans
