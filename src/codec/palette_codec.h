#pragma once

#include "palette_extractor.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hakonyans {

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
    static constexpr uint8_t kStreamV3Magic = 0x41;
    static constexpr uint8_t kStreamV4Magic = 0x42; // signed 16-bit palette colors
    static constexpr uint8_t kFlagMaskDict = 0x01;
    static constexpr uint8_t kFlagPaletteDict = 0x02;

    struct PalKey {
        uint8_t size = 0;
        std::array<int16_t, 8> colors{};
        bool operator==(const PalKey& o) const { return size == o.size && colors == o.colors; }
    };

    struct PalKeyHash {
        size_t operator()(const PalKey& k) const {
            uint64_t h = (uint64_t)k.size * 0x9E3779B97F4A7C15ULL;
            for (int i = 0; i < k.size && i < 8; i++) {
                uint64_t v = (uint16_t)k.colors[i];
                h ^= (v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2));
            }
            return (size_t)h;
        }
    };

    static PalKey make_pal_key(const Palette& p) {
        PalKey k;
        k.size = p.size;
        for (int i = 0; i < p.size && i < 8; i++) k.colors[i] = p.colors[i];
        return k;
    }

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
        std::vector<Palette> palettes, // copy intended, we modify in place
        std::vector<std::vector<uint8_t>> indices_list, // copy intended
        bool enable_palette_dict = false,
        int* out_reorder_trials = nullptr,
        int* out_reorder_adopted = nullptr
    ) {
        // Phase 9s-4: Optimize Order
        int trials = 0;
        int adopted = 0;
        for (size_t i = 0; i < palettes.size(); i++) {
            if (i < indices_list.size()) {
                PaletteExtractor::optimize_palette_order(palettes[i], indices_list[i], trials, adopted);
            }
        }
        if (out_reorder_trials) *out_reorder_trials = trials;
        if (out_reorder_adopted) *out_reorder_adopted = adopted;
        
        // ... continue with encoding ...
        std::vector<uint8_t> out;
        if (palettes.empty()) return out;

        // v2 format:
        // [magic=0x40][flags]
        //   if flags&1: [dict_count:u8][dict masks: dict_count * 8 bytes]
        // v3 format (optional):
        // [magic=0x41][flags]
        //   if flags&1: [dict_count:u8][dict masks: dict_count * 8 bytes]
        //   if flags&2: [pal_dict_count:u8][entries...]
        //       entry = [size:u8][colors:size bytes]
        // Then per block:
        //   [head][palette colors?][indices payload]
        //   head bit7: use_prev
        //   head bit6: use_palette_dict_ref (v3, when !use_prev)
        //   head bit2..0: palette size-1
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
            if (dict_size < raw_size) flags |= kFlagMaskDict;
        }

        bool use_wide_colors = false;
        for (const auto& p : palettes) {
            for (int k = 0; k < p.size; k++) {
                if (p.colors[k] < -128 || p.colors[k] > 127) {
                    use_wide_colors = true;
                    break;
                }
            }
            if (use_wide_colors) break;
        }

        // Optional v3 palette dictionary for non-consecutive recurring palettes.
        std::vector<Palette> palette_dict;
        std::unordered_map<PalKey, uint8_t, PalKeyHash> pal_to_id;
        bool use_v3 = false;
        if (enable_palette_dict) {
            std::unordered_map<PalKey, uint32_t, PalKeyHash> nonprev_counts;
            std::unordered_map<PalKey, Palette, PalKeyHash> key_palette;

            Palette prev_for_stats;
            for (const auto& p : palettes) {
                bool use_prev = (p == prev_for_stats && p.size > 0);
                if (!use_prev && p.size >= 2) {
                    PalKey key = make_pal_key(p);
                    nonprev_counts[key]++;
                    key_palette[key] = p;
                }
                if (!use_prev) prev_for_stats = p;
            }

            struct PalCand {
                PalKey key;
                Palette p;
                int gain;
                uint32_t count;
            };
            std::vector<PalCand> cands;
            cands.reserve(nonprev_counts.size());
            for (const auto& kv : nonprev_counts) {
                const PalKey& key = kv.first;
                uint32_t m = kv.second;
                const Palette& p = key_palette[key];
                if (p.size < 2) continue;
                int color_bytes = use_wide_colors ? 2 : 1;
                // Raw per occurrence: p.size * color_bytes bytes of colors.
                // Dict: 1-byte ref per occurrence + one dict entry [size + colors].
                int gain = (int)(m * (uint32_t)(p.size * color_bytes)) -
                           (int)(m + 1 + (uint32_t)(p.size * color_bytes));
                if (gain > 0) cands.push_back({key, p, gain, m});
            }
            std::sort(cands.begin(), cands.end(), [](const PalCand& a, const PalCand& b) {
                if (a.gain != b.gain) return a.gain > b.gain;
                if (a.count != b.count) return a.count > b.count;
                return a.p.size > b.p.size;
            });

            for (const auto& c : cands) {
                if (palette_dict.size() >= 255) break;
                uint8_t id = (uint8_t)palette_dict.size();
                palette_dict.push_back(c.p);
                pal_to_id[c.key] = id;
            }
            if (!palette_dict.empty()) {
                flags |= kFlagPaletteDict;
                use_v3 = true;
            }
        }

        auto write_color = [&](int16_t c) {
            if (use_wide_colors) {
                uint16_t v = (uint16_t)c;
                out.push_back((uint8_t)(v & 0xFF));
                out.push_back((uint8_t)((v >> 8) & 0xFF));
            } else {
                out.push_back((uint8_t)((int)c + 128));
            }
        };

        out.push_back(use_wide_colors ? kStreamV4Magic : (use_v3 ? kStreamV3Magic : kStreamV2Magic));
        out.push_back(flags);
        if (flags & kFlagMaskDict) {
            out.push_back((uint8_t)mask_dict.size());
            for (uint64_t mask : mask_dict) {
                for (int b = 0; b < 8; b++) {
                    out.push_back((uint8_t)((mask >> (8 * b)) & 0xFF));
                }
            }
        }
        if (flags & kFlagPaletteDict) {
            out.push_back((uint8_t)palette_dict.size());
            for (const auto& p : palette_dict) {
                out.push_back(p.size);
                for (int k = 0; k < p.size; k++) write_color(p.colors[k]);
            }
        }

        Palette prev_pal;
        for (size_t i = 0; i < palettes.size(); i++) {
            const Palette& p = palettes[i];
            const std::vector<uint8_t>* idx_ptr =
                (i < indices_list.size()) ? &indices_list[i] : nullptr;

            bool use_prev = (p == prev_pal && p.size > 0);
            bool use_dict_ref = false;
            uint8_t dict_ref = 0;
            if (!use_prev && use_v3 && (flags & kFlagPaletteDict) && p.size >= 2) {
                auto it = pal_to_id.find(make_pal_key(p));
                if (it != pal_to_id.end()) {
                    use_dict_ref = true;
                    dict_ref = it->second;
                }
            }

            uint8_t head = (use_prev ? 0x80 : 0) |
                           (use_dict_ref ? 0x40 : 0) |
                           ((p.size - 1) & 0x07);
            out.push_back(head);

            if (!use_prev) {
                if (use_dict_ref) {
                    out.push_back(dict_ref);
                } else {
                    for (int k = 0; k < p.size; k++) write_color(p.colors[k]);
                }
                prev_pal = p;
            }

            if (p.size <= 1) {
                // Solid-color palette block: indices are implicitly all zero.
                continue;
            }

            if (p.size == 2) {
                uint64_t mask = idx_ptr ? indices_to_mask64(*idx_ptr) : 0;
                if (flags & kFlagMaskDict) {
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
            if (idx_ptr) {
                for (uint8_t v : *idx_ptr) bw.write(v, bits);
            } else {
                for (int k = 0; k < 64; k++) bw.write(0, bits);
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
        if (size == 0 || num_blocks <= 0) return;

        size_t pos = 0;
        bool is_v2 = false;
        bool is_v3 = false;
        bool is_v4 = false;
        uint8_t flags = 0;
        std::vector<uint64_t> mask_dict;
        std::vector<Palette> palette_dict;

        if (data[0] == kStreamV2Magic || data[0] == kStreamV3Magic || data[0] == kStreamV4Magic) {
            is_v2 = true;
            is_v3 = (data[0] == kStreamV3Magic || data[0] == kStreamV4Magic);
            is_v4 = (data[0] == kStreamV4Magic);
            pos = 1;
            if (pos < size) flags = data[pos++];

            if (flags & kFlagMaskDict) {
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

            if (is_v3 && (flags & kFlagPaletteDict)) {
                if (pos >= size) return;
                uint8_t pal_dict_count = data[pos++];
                palette_dict.reserve(pal_dict_count);
                for (uint8_t i = 0; i < pal_dict_count; i++) {
                    if (pos >= size) return;
                    Palette p;
                    p.size = data[pos++];
                    if (p.size == 0 || p.size > 8) return;
                    size_t color_bytes = (size_t)p.size * (is_v4 ? 2 : 1);
                    if (pos + color_bytes > size) return;
                    for (int k = 0; k < p.size; k++) {
                        if (is_v4) {
                            uint16_t v = (uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8);
                            p.colors[k] = (int16_t)v;
                            pos += 2;
                        } else {
                            p.colors[k] = (int16_t)data[pos++] - 128;
                        }
                    }
                    palette_dict.push_back(p);
                }
            }
        }

        Palette prev_pal;
        
        for (int i = 0; i < num_blocks; i++) {
            if (pos >= size) break;
            
            uint8_t head = data[pos++];
            bool use_prev = (head & 0x80);
            bool use_dict = is_v3 && !use_prev && (head & 0x40);
            int p_size = (head & 0x07) + 1;
            
            Palette p;
            p.size = p_size;
            
            if (use_prev) {
                p = prev_pal;
            } else if (use_dict) {
                if (pos >= size) return;
                uint8_t dict_idx = data[pos++];
                if (dict_idx >= palette_dict.size()) return;
                p = palette_dict[dict_idx];
                if (p.size != p_size) return;
                prev_pal = p;
            } else {
                size_t color_bytes = (size_t)p_size * (is_v4 ? 2 : 1);
                if (pos + color_bytes > size) return;
                for (int k = 0; k < p_size; k++) {
                    if (is_v4) {
                        uint16_t v = (uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8);
                        p.colors[k] = (int16_t)v;
                        pos += 2;
                    } else {
                        p.colors[k] = (int16_t)data[pos++] - 128;
                    }
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
                if (flags & kFlagMaskDict) {
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
