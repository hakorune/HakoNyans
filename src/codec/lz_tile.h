#pragma once
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
        out.reserve(src.size()); 

        size_t pos = 0;
        const size_t src_size = src.size();

        // Simple Hash Chain Matcher
        // Hash table size 64K
        static constexpr int HASH_BITS = 16;
        static constexpr int HASH_SIZE = 1 << HASH_BITS;
        static constexpr int WINDOW_SIZE = 32768;
        
        // We use a simple "last pos" table for MVP speed/simplicity since it's "Tile Local" (small data)
        // Actually, for better compression we should chain or just use "best recent match".
        // Let's use a single-entry hash table (stores last position of hash) for simplicity and speed (LZ4-ish style).
        // It fits the "2% improvement" requirement easily if data is truly repetitive.
        
        std::vector<int> head(HASH_SIZE, -1);
        
        auto hash = [&](size_t p) -> uint32_t {
            if (p + 3 > src_size) return 0;
            // Simple hash: (b0<<8 ^ b1<<4 ^ b2) 
            // Better: ((val * prime) >> shift)
            uint32_t v = ((uint32_t)src[p] << 16) | ((uint32_t)src[p+1] << 8) | (uint32_t)src[p+2];
            return ((v * 0x1e35a7bd) >> (32 - HASH_BITS));
        };

        size_t lit_start = 0;

        while (pos < src_size) {
            // Early exit check
            if (pos + 3 > src_size) {
                pos++;
                continue;
            }

            uint32_t h = hash(pos);
            int ref = head[h];
            head[h] = (int)pos;

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
                int lit_len = (int)(pos - lit_start);
                while (lit_len > 0) {
                    int chunk = std::min(lit_len, 255);
                    out.push_back(0); // LITRUN
                    out.push_back((uint8_t)chunk);
                    out.insert(out.end(), src.begin() + lit_start, src.begin() + lit_start + chunk);
                    lit_start += chunk;
                    lit_len -= chunk;
                }

                // Write match
                // [1][len][dist]
                out.push_back(1); // MATCH
                out.push_back((uint8_t)best_len);
                out.push_back((uint8_t)(best_dist & 0xFF));
                out.push_back((uint8_t)((best_dist >> 8) & 0xFF));

                pos += best_len;
                lit_start = pos;
            } else {
                pos++;
            }
        }

        // Flush remaining literals
        int lit_len = (int)(src_size - lit_start);
        while (lit_len > 0) {
            int chunk = std::min(lit_len, 255);
            out.push_back(0); // LITRUN
            out.push_back((uint8_t)chunk);
            out.insert(out.end(), src.begin() + lit_start, src.begin() + lit_start + chunk);
            lit_start += chunk;
            lit_len -= chunk;
        }

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
