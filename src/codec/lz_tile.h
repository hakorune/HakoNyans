#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
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
        const size_t src_size = src.size();
        std::vector<uint8_t> out;
        out.reserve(src_size + (src_size / 255u + 1u) * 2u);
        if (src_size < 3) {
            out.push_back(0);
            out.push_back((uint8_t)src_size);
            out.insert(out.end(), src.begin(), src.end());
            return out;
        }

        const RuntimeParams params = runtime_params();
        size_t pos = 0;
        size_t lit_start = 0;

        static constexpr int HASH_BITS = 16;
        static constexpr int HASH_SIZE = 1 << HASH_BITS;

        thread_local std::array<int, HASH_SIZE> head;
        thread_local std::array<uint32_t, HASH_SIZE> stamp;
        thread_local uint32_t epoch = 1;
        std::vector<int> prev;

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

        auto hash3 = [&](size_t p) -> uint32_t {
            if (p + 3 > src_size) return 0u;
            uint32_t v = ((uint32_t)src[p] << 16) |
                         ((uint32_t)src[p + 1] << 8) |
                         (uint32_t)src[p + 2];
            return ((v * 0x1e35a7bd) >> (32 - HASH_BITS));
        };

        auto add_pos = [&](size_t p) {
            if (p + 3 > src_size) return;
            uint32_t h = hash3(p);
            int old = head_get(h);
            prev[p] = old;
            head_set(h, (int)p);
        };

        auto match_len_from = [&](size_t ref_pos, size_t cur_pos) -> int {
            const size_t max_len = std::min<size_t>(255u, src_size - cur_pos);
            int len = 3;
            const uint8_t* a = src.data() + ref_pos + 3;
            const uint8_t* b = src.data() + cur_pos + 3;
            size_t remain = max_len - 3;

            while (remain >= 8) {
                uint64_t va = 0, vb = 0;
                std::memcpy(&va, a, 8);
                std::memcpy(&vb, b, 8);
                if (va != vb) break;
                a += 8;
                b += 8;
                len += 8;
                remain -= 8;
            }
            while (remain > 0 && *a == *b) {
                ++a;
                ++b;
                ++len;
                --remain;
            }
            return len;
        };

        struct Match {
            int len = 0;
            int dist = 0;
        };

        auto find_best_match = [&](size_t cur_pos) -> Match {
            Match best{};
            if (cur_pos + 3 > src_size) return best;

            int ref = head_get(hash3(cur_pos));
            int depth = 0;
            while (ref >= 0 && depth < params.chain_depth) {
                size_t ref_pos = (size_t)ref;
                int dist = (int)(cur_pos - ref_pos);
                if (dist <= 0) {
                    ref = prev[ref_pos];
                    depth++;
                    continue;
                }
                if (dist > params.window_size) break;

                if (src[ref_pos] == src[cur_pos] &&
                    src[ref_pos + 1] == src[cur_pos + 1] &&
                    src[ref_pos + 2] == src[cur_pos + 2]) {
                    int len = match_len_from(ref_pos, cur_pos);
                    if (len > best.len || (len == best.len && dist < best.dist)) {
                        best.len = len;
                        best.dist = dist;
                        if (len >= params.nice_length) break;
                    }
                }
                ref = prev[ref_pos];
                depth++;
            }
            return best;
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

        // Keep the historical fast path as default behavior. Advanced hash-chain
        // search is opt-in via HKN_TILELZ_CHAIN_DEPTH>0 or lazy strategy.
        if (params.chain_depth <= 0 && params.match_strategy == 0) {
            while (pos < src_size) {
                if (pos + 3 > src_size) {
                    pos++;
                    continue;
                }

                const uint32_t h = hash3(pos);
                const int ref = head_get(h);
                head_set(h, (int)pos);

                int best_len = 0;
                int best_dist = 0;
                if (ref >= 0 && (size_t)ref < pos) {
                    const size_t dist = pos - (size_t)ref;
                    if (dist <= (size_t)params.window_size &&
                        src[(size_t)ref] == src[pos] &&
                        src[(size_t)ref + 1] == src[pos + 1] &&
                        src[(size_t)ref + 2] == src[pos + 2]) {
                        int len = 3;
                        while (pos + (size_t)len < src_size &&
                               len < 255 &&
                               src[(size_t)ref + (size_t)len] == src[pos + (size_t)len]) {
                            len++;
                        }
                        best_len = len;
                        best_dist = (int)dist;
                    }
                }

                if (best_len >= 3) {
                    flush_literals(lit_start, pos);

                    const size_t off = out.size();
                    out.resize(off + 4);
                    out[off] = 1;
                    out[off + 1] = (uint8_t)best_len;
                    out[off + 2] = (uint8_t)(best_dist & 0xFF);
                    out[off + 3] = (uint8_t)((best_dist >> 8) & 0xFF);

                    pos += (size_t)best_len;
                    lit_start = pos;
                } else {
                    pos++;
                }
            }

            flush_literals(lit_start, src_size);
            return out;
        }

        prev.assign(src_size, -1);

        while (pos < src_size) {
            Match best = find_best_match(pos);
            if (best.len >= 3) {
                add_pos(pos);

                if (params.match_strategy == 1 && pos + 1 < src_size) {
                    Match next = find_best_match(pos + 1);
                    if (next.len > best.len + 1) {
                        pos++;
                        continue;
                    }
                }

                flush_literals(lit_start, pos);

                const size_t off = out.size();
                out.resize(off + 4);
                out[off] = 1;
                out[off + 1] = (uint8_t)best.len;
                out[off + 2] = (uint8_t)(best.dist & 0xFF);
                out[off + 3] = (uint8_t)((best.dist >> 8) & 0xFF);

                for (size_t p = pos + 1; p < pos + (size_t)best.len; ++p) {
                    add_pos(p);
                }
                pos += (size_t)best.len;
                lit_start = pos;
            } else {
                add_pos(pos);
                pos++;
            }
        }

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

private:
    struct RuntimeParams {
        int chain_depth;
        int window_size;
        int nice_length;
        int match_strategy; // 0=greedy, 1=lazy1
    };

    static int parse_env_int(const char* key, int fallback, int min_v, int max_v) {
        const char* raw = std::getenv(key);
        if (!raw || raw[0] == '\0') return fallback;
        char* end = nullptr;
        errno = 0;
        long v = std::strtol(raw, &end, 10);
        if (errno != 0 || end == raw || *end != '\0') return fallback;
        if (v < (long)min_v || v > (long)max_v) return fallback;
        return (int)v;
    }

    static RuntimeParams runtime_params() {
        static const RuntimeParams p = {
            parse_env_int("HKN_TILELZ_CHAIN_DEPTH", 0, 0, 128),
            parse_env_int("HKN_TILELZ_WINDOW_SIZE", 32768, 1024, 65535),
            parse_env_int("HKN_TILELZ_NICE_LENGTH", 32, 3, 255),
            parse_env_int("HKN_TILELZ_MATCH_STRATEGY", 0, 0, 1),
        };
        return p;
    }
};

} // namespace hakonyans
