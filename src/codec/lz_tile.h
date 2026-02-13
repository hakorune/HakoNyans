#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace hakonyans {

class TileLZ {
public:
    static constexpr uint8_t COPY_MAGIC = 0xA8;
    static constexpr uint8_t BLOCK_TYPES_MAGIC = 0xA6;
    static constexpr uint8_t PALETTE_MAGIC = 0xA7;

    static std::vector<uint8_t> compress(const std::vector<uint8_t>& src) {
        if (src.empty()) return {};
        std::vector<uint8_t> out;
        out.reserve(src.size() + (src.size() / 255u + 1u) * 2u);

        size_t pos = 0;
        const size_t src_size = src.size();

        // Simple Hash Chain Matcher
        // Hash table size 64K
        static constexpr int HASH_BITS = 16;
        static constexpr int HASH_SIZE = 1 << HASH_BITS;
        static constexpr int WINDOW_SIZE = 32768;
        
        thread_local std::array<int, HASH_SIZE> head;
        thread_local std::array<uint32_t, HASH_SIZE> stamp;
        thread_local uint32_t epoch = 1;
        epoch++;
        if (epoch == 0) {
            stamp.fill(0);
            epoch = 1;
        }

        auto head_get = [&](uint32_t h) -> int {
            if (stamp[h] != epoch) return -1;
            return head[h];
        };

        auto head_set = [&](uint32_t h, int p) {
            stamp[h] = epoch;
            head[h] = p;
        };
        
        auto hash = [&](size_t p) -> uint32_t {
            if (p + 3 > src_size) return 0;
            // Simple hash: (b0<<8 ^ b1<<4 ^ b2) 
            // Better: ((val * prime) >> shift)
            uint32_t v = ((uint32_t)src[p] << 16) | ((uint32_t)src[p+1] << 8) | (uint32_t)src[p+2];
            return ((v * 0x1e35a7bd) >> (32 - HASH_BITS));
        };

        auto flush_literals = [&](size_t from, size_t to) {
            size_t lit_len = to - from;
            while (lit_len > 0) {
                const size_t chunk = std::min<size_t>(lit_len, 255u);
                const size_t off = out.size();
                out.resize(off + 2 + chunk);
                out[off] = 0; // LITRUN
                out[off + 1] = (uint8_t)chunk;
                std::memcpy(out.data() + off + 2, src.data() + from, chunk);
                from += chunk;
                lit_len -= chunk;
            }
        };

        size_t lit_start = 0;

        while (pos < src_size) {
            // Early exit check
            if (pos + 3 > src_size) {
                pos++;
                continue;
            }

            uint32_t h = hash(pos);
            int ref = head_get(h);
            head_set(h, (int)pos);

            // Check match if ref is valid and within sliding window
            bool match = false;
            int best_len = 0;
            int best_dist = 0;

            if (ref >= 0 && (size_t)ref < pos && (pos - ref) <= WINDOW_SIZE) {
                if (src[ref] == src[pos] && src[ref+1] == src[pos+1] && src[ref+2] == src[pos+2]) {
                    // Match found! Extend.
                    int len = 3;
                    while (pos + len < src_size && len < 255 && src[ref + len] == src[pos + len]) {
                        len++;
                    }
                    best_len = len;
                    best_dist = (int)(pos - ref);
                    match = true;
                }
            }

            if (match && best_len >= 3) {
                // Flush literals
                flush_literals(lit_start, pos);

                // Write match
                // [1][len][dist]
                const size_t off = out.size();
                out.resize(off + 4);
                out[off] = 1; // MATCH
                out[off + 1] = (uint8_t)best_len;
                out[off + 2] = (uint8_t)(best_dist & 0xFF);
                out[off + 3] = (uint8_t)((best_dist >> 8) & 0xFF);

                pos += best_len;
                lit_start = pos;
            } else {
                pos++;
            }
        }

        // Flush remaining literals
        flush_literals(lit_start, src_size);

        return out;
    }

    static std::vector<uint8_t> decompress(const uint8_t* val, size_t sz, size_t raw_count) {
        std::vector<uint8_t> out;
        out.reserve(raw_count);

        size_t pos = 0;
        while (pos < sz) {
            if (pos >= sz) break;
            uint8_t tag = val[pos++];

            if (tag == 0) { // LITRUN
                if (pos >= sz) break;
                int len = val[pos++];
                if (len == 0) continue; 
                if (pos + len > sz) break;
                out.insert(out.end(), val + pos, val + pos + len);
                pos += len;
            } else if (tag == 1) { // MATCH
                if (pos >= sz) break; 
                int len = val[pos++];
                if (pos + 2 > sz) break;
                uint16_t dist = val[pos] | (val[pos+1] << 8);
                pos += 2;

                if (dist == 0 || dist > out.size()) {
                    // Invalid history distance
                    // Error recovery: fill with zeros or truncate? 
                    // Safe policy: Stop decoding this stream to avoid OOB read.
                    break; 
                }

                size_t start_idx = out.size() - dist;
                // manual loop to handle overlap correctly/safely
                for (int i = 0; i < len; i++) {
                    out.push_back(out[start_idx + i]);
                }
            } else {
                // Unknown tag
                break;
            }
        }
        
        // Safety check: if output is smaller than expected, we might pad or leave it (caller handles mismatch)
        // If larger (should not happen with reserve), vector handles it.
        return out;
    }
    
    // Decompress variant that takes reference to vector for efficiency
    static bool decompress_to(const uint8_t* val, size_t sz, std::vector<uint8_t>& out, size_t raw_count) {
        out.clear();
        out.reserve(raw_count);

        size_t pos = 0;
        while (pos < sz && out.size() < raw_count) {
            if (pos >= sz) break;
            uint8_t tag = val[pos++];

            if (tag == 0) { // LITRUN
                if (pos >= sz) break;
                int len = val[pos++];
                if (len == 0) continue; 
                if (pos + len > sz) break;
                // Clamp len if it exceeds raw_count
                if (out.size() + len > raw_count) len = (int)(raw_count - out.size());
                
                out.insert(out.end(), val + pos, val + pos + len);
                pos += len;
            } else if (tag == 1) { // MATCH
                if (pos >= sz) break; 
                int len = val[pos++];
                if (pos + 2 > sz) break;
                uint16_t dist = val[pos] | (val[pos+1] << 8);
                pos += 2;

                 if (dist == 0 || dist > out.size()) {
                    return false; // Error
                }

                // Clamp len
                if (out.size() + len > raw_count) len = (int)(raw_count - out.size());

                size_t start_idx = out.size() - dist;
                for (int i = 0; i < len; i++) {
                    out.push_back(out[start_idx + i]);
                }
            } else {
                return false; // Unknown tag
            }
        }
        return (out.size() == raw_count);
    }
};

} // namespace hakonyans
