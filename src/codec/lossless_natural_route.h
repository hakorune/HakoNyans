#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "../platform/thread_budget.h"
#include "zigzag.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <future>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace hakonyans::lossless_natural_route {

namespace detail {

struct GlobalChainLzParams {
    int window_size = 65535;
    int chain_depth = 32;
    int min_dist_len3 = 128;
    int bias_permille = 990;
    int nice_length = 255;
    int match_strategy = 0; // 0=greedy, 1=lazy1, 2=optparse_dp (max lane)
    int opt_max_matches = 4;
    int opt_lit_max = 128;
    int opt_memcap_mb = 64;
    int opt_probe_src_max_bytes = 2 * 1024 * 1024;
    int opt_probe_ratio_min_x1000 = 20;
    int opt_probe_ratio_max_x1000 = 80;
    int opt_min_gain_bytes = 512;
};

struct GlobalChainLzCounters {
    uint64_t calls = 0;
    uint64_t src_bytes = 0;
    uint64_t out_bytes = 0;
    uint64_t match_count = 0;
    uint64_t match_bytes = 0;
    uint64_t literal_bytes = 0;
    uint64_t chain_steps = 0;
    uint64_t depth_limit_hits = 0;
    uint64_t early_maxlen_hits = 0;
    uint64_t nice_cutoff_hits = 0;
    uint64_t len3_reject_dist = 0;
    uint64_t optparse_enabled = 0;
    uint64_t optparse_fallback_count = 0;
    uint64_t optparse_fallback_memcap = 0;
    uint64_t optparse_fallback_allocfail = 0;
    uint64_t optparse_fallback_unreachable = 0;
    uint64_t optparse_dp_positions = 0;
    uint64_t optparse_lit_edges_eval = 0;
    uint64_t optparse_match_edges_eval = 0;
    uint64_t optparse_tokens_litrun = 0;
    uint64_t optparse_tokens_match = 0;
    uint64_t optparse_chose_shorter_than_longest = 0;
    uint64_t optparse_probe_accept = 0;
    uint64_t optparse_probe_reject = 0;
    uint64_t optparse_adopt = 0;
    uint64_t optparse_reject_small_gain = 0;
};

inline int parse_lz_env_int(const char* key, int fallback, int min_v, int max_v) {
    const char* raw = std::getenv(key);
    if (!raw || raw[0] == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0') return fallback;
    if (v < (long)min_v || v > (long)max_v) return fallback;
    return (int)v;
}

inline const GlobalChainLzParams& global_chain_lz_runtime_params() {
    static const GlobalChainLzParams p = []() {
        GlobalChainLzParams t{};
        t.window_size = parse_lz_env_int("HKN_LZ_WINDOW_SIZE", 65535, 1024, 65535);
        t.chain_depth = parse_lz_env_int("HKN_LZ_CHAIN_DEPTH", 32, 1, 128);
        t.min_dist_len3 = parse_lz_env_int("HKN_LZ_MIN_DIST_LEN3", 128, 0, 65535);
        t.bias_permille = parse_lz_env_int("HKN_LZ_BIAS_PERMILLE", 990, 900, 1100);
        t.nice_length = parse_lz_env_int("HKN_LZ_NICE_LENGTH", 255, 4, 255);
        t.match_strategy = parse_lz_env_int("HKN_LZ_MATCH_STRATEGY", 0, 0, 2);
        t.opt_max_matches = parse_lz_env_int("HKN_LZ_OPTPARSE_MAX_MATCHES", 4, 1, 32);
        t.opt_lit_max = parse_lz_env_int("HKN_LZ_OPTPARSE_LIT_MAX", 128, 1, 255);
        t.opt_memcap_mb = parse_lz_env_int("HKN_LZ_OPTPARSE_MEMCAP_MB", 64, 4, 1024);
        t.opt_probe_src_max_bytes = parse_lz_env_int(
            "HKN_LZ_OPTPARSE_PROBE_SRC_MAX", 2 * 1024 * 1024, 65536, 64 * 1024 * 1024
        );
        t.opt_probe_ratio_min_x1000 = parse_lz_env_int(
            "HKN_LZ_OPTPARSE_PROBE_RATIO_MIN", 20, 0, 1000
        );
        t.opt_probe_ratio_max_x1000 = parse_lz_env_int(
            "HKN_LZ_OPTPARSE_PROBE_RATIO_MAX", 80, 0, 1000
        );
        t.opt_min_gain_bytes = parse_lz_env_int(
            "HKN_LZ_OPTPARSE_MIN_GAIN_BYTES", 512, 0, 1 << 20
        );
        return t;
    }();
    return p;
}

struct OptparseTok {
    uint8_t kind = 0; // 0=litrun, 1=match
    uint8_t len = 0;
    uint16_t dist = 0;
};

inline bool optparse_tok_prefer(const OptparseTok& a, const OptparseTok& b) {
    if (a.kind != b.kind) return a.kind > b.kind; // prefer match over litrun
    if (a.kind == 1) {
        if (a.len != b.len) return a.len > b.len;
        if (a.dist != b.dist) return a.dist < b.dist;
        return false;
    }
    if (a.len != b.len) return a.len > b.len;
    return false;
}

inline bool compress_global_chain_lz_optparse(
    const std::vector<uint8_t>& src, const GlobalChainLzParams& p,
    GlobalChainLzCounters* counters, std::vector<uint8_t>* out
) {
    if (!out) return false;
    out->clear();
    if (src.empty()) return true;

    const size_t src_size = src.size();
    const uint8_t* s = src.data();
    const size_t state_count = src_size + 1;

    const size_t approx_per_state =
        sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) +
        sizeof(int32_t) + sizeof(OptparseTok) + sizeof(uint8_t) + sizeof(uint64_t);
    const size_t approx_bytes = state_count * approx_per_state;
    const size_t memcap_bytes = (size_t)std::max(1, p.opt_memcap_mb) * 1024u * 1024u;
    if (approx_bytes > memcap_bytes) {
        if (counters) {
            counters->optparse_fallback_count++;
            counters->optparse_fallback_memcap++;
        }
        return false;
    }

    std::vector<uint64_t> dp_cost;
    std::vector<uint32_t> dp_bytes;
    std::vector<uint32_t> dp_tokens;
    std::vector<int32_t> prev_pos;
    std::vector<OptparseTok> prev_tok;
    std::vector<uint8_t> longest_match_at_pos;
    std::vector<uint64_t> literal_cost_prefix;
    try {
        dp_cost.assign(state_count, std::numeric_limits<uint64_t>::max() / 4u);
        dp_bytes.assign(state_count, std::numeric_limits<uint32_t>::max());
        dp_tokens.assign(state_count, std::numeric_limits<uint32_t>::max());
        prev_pos.assign(state_count, -1);
        prev_tok.assign(state_count, OptparseTok{});
        longest_match_at_pos.assign(src_size, 0);
        literal_cost_prefix.assign(state_count, 0);
    } catch (const std::bad_alloc&) {
        if (counters) {
            counters->optparse_fallback_count++;
            counters->optparse_fallback_allocfail++;
        }
        return false;
    }

    std::array<uint16_t, 256> byte_cost{};
    byte_cost.fill(256); // Q8 fixed-point: 1 byte ~= 8 bits
    for (size_t i = 0; i < src_size; i++) {
        literal_cost_prefix[i + 1] = literal_cost_prefix[i] + (uint64_t)byte_cost[s[i]];
    }

    constexpr int HASH_BITS = 16;
    constexpr int HASH_SIZE = 1 << HASH_BITS;
    const int window_size = p.window_size;
    const int chain_depth = p.chain_depth;
    const int min_dist_len3 = p.min_dist_len3;
    const int nice_length = p.nice_length;
    const int opt_max_matches = std::min(32, std::max(1, p.opt_max_matches));
    const int opt_lit_max = std::max(1, p.opt_lit_max);

    thread_local std::array<int, HASH_SIZE> head{};
    thread_local std::array<uint32_t, HASH_SIZE> head_epoch{};
    thread_local uint32_t epoch = 1;
    epoch++;
    if (epoch == 0) {
        head_epoch.fill(0);
        epoch = 1;
    }

    auto head_get = [&](uint32_t h) -> int {
        return (head_epoch[h] == epoch) ? head[h] : -1;
    };
    auto head_set = [&](uint32_t h, int pos) {
        head_epoch[h] = epoch;
        head[h] = pos;
    };

    thread_local std::vector<int> prev;
    if (prev.size() < src_size) prev.resize(src_size);

    auto hash3 = [&](size_t pos) -> uint32_t {
        uint32_t v = ((uint32_t)s[pos] << 16) |
                     ((uint32_t)s[pos + 1] << 8) |
                     (uint32_t)s[pos + 2];
        return (v * 0x1e35a7bdu) >> (32 - HASH_BITS);
    };

    auto match_len_from = [&](size_t ref_pos, size_t cur_pos) -> int {
        const size_t max_len = std::min<size_t>(255, src_size - cur_pos);
        int len = 3;
        const uint8_t* a = s + ref_pos + 3;
        const uint8_t* b = s + cur_pos + 3;
        size_t remain = max_len - 3;
        while (remain >= sizeof(uint64_t)) {
            uint64_t va = 0;
            uint64_t vb = 0;
            std::memcpy(&va, a, sizeof(uint64_t));
            std::memcpy(&vb, b, sizeof(uint64_t));
            if (va != vb) {
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
                uint64_t diff = va ^ vb;
#if defined(__GNUC__) || defined(__clang__)
                const int common = (int)(__builtin_ctzll(diff) >> 3);
#else
                int common = 0;
                while ((diff & 0xFFu) == 0u && common < 8) {
                    diff >>= 8;
                    common++;
                }
#endif
                len += common;
                return len;
#else
                int common = 0;
                while (common < 8 && a[common] == b[common]) common++;
                len += common;
                return len;
#endif
            }
            a += sizeof(uint64_t);
            b += sizeof(uint64_t);
            len += (int)sizeof(uint64_t);
            remain -= sizeof(uint64_t);
        }
        while (remain > 0 && *a == *b) {
            ++a;
            ++b;
            ++len;
            --remain;
        }
        return len;
    };

    struct MatchCandidate {
        uint8_t len = 0;
        uint16_t dist = 0;
    };

    auto collect_matches = [&](size_t cur_pos, std::array<MatchCandidate, 32>& cands,
                               int& cand_count, int& longest_len) {
        cand_count = 0;
        longest_len = 0;
        if (cur_pos + 2 >= src_size) return;
        int ref = head_get(hash3(cur_pos));
        int depth = 0;
        while (ref >= 0 && depth < chain_depth) {
            if (counters) counters->chain_steps++;
            const size_t ref_pos = (size_t)ref;
            const int dist = (int)(cur_pos - ref_pos);
            if (dist > 0 && dist <= window_size) {
                if (s[ref_pos] == s[cur_pos] &&
                    s[ref_pos + 1] == s[cur_pos + 1] &&
                    s[ref_pos + 2] == s[cur_pos + 2]) {
                    int len = 3;
                    if (cur_pos + 3 < src_size && s[ref_pos + 3] == s[cur_pos + 3]) {
                        len = match_len_from(ref_pos, cur_pos);
                    }
                    const bool acceptable = (len >= 4) || (len == 3 && dist <= min_dist_len3);
                    if (!acceptable && len == 3 && dist > min_dist_len3 && counters) {
                        counters->len3_reject_dist++;
                    }
                    if (acceptable) {
                        longest_len = std::max(longest_len, len);
                        bool duplicate = false;
                        for (int i = 0; i < cand_count; i++) {
                            if ((int)cands[i].len == len && (int)cands[i].dist == dist) {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate) {
                            MatchCandidate m{};
                            m.len = (uint8_t)len;
                            m.dist = (uint16_t)dist;
                            if (cand_count < opt_max_matches) {
                                cands[cand_count++] = m;
                            } else {
                                int worst_idx = 0;
                                for (int i = 1; i < cand_count; i++) {
                                    if (cands[i].len < cands[worst_idx].len ||
                                        (cands[i].len == cands[worst_idx].len &&
                                         cands[i].dist > cands[worst_idx].dist)) {
                                        worst_idx = i;
                                    }
                                }
                                if (m.len > cands[worst_idx].len ||
                                    (m.len == cands[worst_idx].len &&
                                     m.dist < cands[worst_idx].dist)) {
                                    cands[worst_idx] = m;
                                }
                            }
                        }
                        if (len == 255) {
                            if (counters) counters->early_maxlen_hits++;
                            break;
                        }
                        if (len >= nice_length) {
                            if (counters) counters->nice_cutoff_hits++;
                            break;
                        }
                    }
                }
            } else if (dist > window_size) {
                break;
            }
            ref = prev[ref_pos];
            depth++;
        }
        if (ref >= 0 && depth >= chain_depth && counters) counters->depth_limit_hits++;
        if (cand_count > 1) {
            std::sort(cands.begin(), cands.begin() + cand_count, [](const MatchCandidate& a,
                                                                     const MatchCandidate& b) {
                if (a.len != b.len) return a.len > b.len;
                return a.dist < b.dist;
            });
        }
    };

    dp_cost[0] = 0;
    dp_bytes[0] = 0;
    dp_tokens[0] = 0;

    auto relax = [&](size_t from, size_t to, const OptparseTok& tok,
                     uint64_t delta_cost, uint32_t delta_bytes) {
        const uint64_t from_cost = dp_cost[from];
        if (from_cost >= (std::numeric_limits<uint64_t>::max() / 8u)) return;
        const uint64_t new_cost = from_cost + delta_cost;
        const uint32_t new_bytes = dp_bytes[from] + delta_bytes;
        const uint32_t new_tokens = dp_tokens[from] + 1u;

        bool take = false;
        if (new_cost < dp_cost[to]) {
            take = true;
        } else if (new_cost == dp_cost[to]) {
            if (new_bytes < dp_bytes[to]) {
                take = true;
            } else if (new_bytes == dp_bytes[to]) {
                if (new_tokens < dp_tokens[to]) {
                    take = true;
                } else if (new_tokens == dp_tokens[to] &&
                           prev_pos[to] >= 0 &&
                           optparse_tok_prefer(tok, prev_tok[to])) {
                    take = true;
                }
            }
        }
        if (!take) return;
        dp_cost[to] = new_cost;
        dp_bytes[to] = new_bytes;
        dp_tokens[to] = new_tokens;
        prev_pos[to] = (int32_t)from;
        prev_tok[to] = tok;
    };

    for (size_t pos = 0; pos < src_size; pos++) {
        if (dp_cost[pos] < (std::numeric_limits<uint64_t>::max() / 8u)) {
            if (counters) counters->optparse_dp_positions++;
            const int max_lit =
                std::min<int>({255, opt_lit_max, (int)(src_size - pos)});
            if (counters) counters->optparse_lit_edges_eval += (uint64_t)max_lit;
            for (int len = 1; len <= max_lit; len++) {
                const size_t next = pos + (size_t)len;
                const uint64_t lit_body_cost =
                    literal_cost_prefix[next] - literal_cost_prefix[pos];
                const uint64_t token_cost =
                    (uint64_t)byte_cost[0] + (uint64_t)byte_cost[(uint8_t)len] +
                    lit_body_cost;
                relax(
                    pos, next, OptparseTok{0, (uint8_t)len, 0}, token_cost,
                    (uint32_t)(2 + len)
                );
            }

            std::array<MatchCandidate, 32> cands{};
            int cand_count = 0;
            int longest_len = 0;
            collect_matches(pos, cands, cand_count, longest_len);
            longest_match_at_pos[pos] = (uint8_t)std::min(255, longest_len);
            if (counters) counters->optparse_match_edges_eval += (uint64_t)cand_count;
            for (int i = 0; i < cand_count; i++) {
                const int len = (int)cands[i].len;
                const uint16_t dist = cands[i].dist;
                const size_t next = pos + (size_t)len;
                if (next > src_size) continue;
                const uint64_t token_cost =
                    (uint64_t)byte_cost[1] +
                    (uint64_t)byte_cost[(uint8_t)len] +
                    (uint64_t)byte_cost[(uint8_t)(dist & 0xFF)] +
                    (uint64_t)byte_cost[(uint8_t)(dist >> 8)];
                relax(
                    pos, next, OptparseTok{1, (uint8_t)len, dist}, token_cost, 4u
                );
            }
        }

        if (pos + 2 < src_size) {
            const uint32_t h = hash3(pos);
            prev[pos] = head_get(h);
            head_set(h, (int)pos);
        }
    }

    if (dp_cost[src_size] >= (std::numeric_limits<uint64_t>::max() / 8u)) {
        if (counters) {
            counters->optparse_fallback_count++;
            counters->optparse_fallback_unreachable++;
        }
        return false;
    }

    std::vector<OptparseTok> tokens;
    tokens.reserve((size_t)dp_tokens[src_size]);
    for (size_t cur = src_size; cur > 0;) {
        const int32_t pre = prev_pos[cur];
        if (pre < 0) {
            if (counters) {
                counters->optparse_fallback_count++;
                counters->optparse_fallback_unreachable++;
            }
            return false;
        }
        tokens.push_back(prev_tok[cur]);
        cur = (size_t)pre;
    }
    std::reverse(tokens.begin(), tokens.end());

    out->clear();
    out->reserve((size_t)dp_bytes[src_size] + 8u);
    size_t pos_idx = 0;
    for (const auto& tok : tokens) {
        const size_t start_pos = pos_idx;
        if (tok.kind == 0) {
            out->push_back(0);
            out->push_back(tok.len);
            out->insert(out->end(), s + pos_idx, s + pos_idx + tok.len);
            pos_idx += (size_t)tok.len;
            if (counters) {
                counters->literal_bytes += (uint64_t)tok.len;
                counters->optparse_tokens_litrun++;
            }
        } else {
            out->push_back(1);
            out->push_back(tok.len);
            out->push_back((uint8_t)(tok.dist & 0xFF));
            out->push_back((uint8_t)(tok.dist >> 8));
            pos_idx += (size_t)tok.len;
            if (counters) {
                counters->match_count++;
                counters->match_bytes += (uint64_t)tok.len;
                counters->optparse_tokens_match++;
                if (start_pos < longest_match_at_pos.size() &&
                    longest_match_at_pos[start_pos] > tok.len) {
                    counters->optparse_chose_shorter_than_longest++;
                }
            }
        }
    }

    if (pos_idx != src_size) {
        if (counters) {
            counters->optparse_fallback_count++;
            counters->optparse_fallback_unreachable++;
        }
        return false;
    }
    return true;
}

inline std::vector<uint8_t> compress_global_chain_lz(
    const std::vector<uint8_t>& src, const GlobalChainLzParams& p,
    GlobalChainLzCounters* counters = nullptr
) {
    if (src.empty()) return {};

    constexpr int HASH_BITS = 16;
    constexpr int HASH_SIZE = 1 << HASH_BITS;
    const int window_size = p.window_size;
    const int chain_depth = p.chain_depth;
    const int min_dist_len3 = p.min_dist_len3;
    const int nice_length = p.nice_length;
    bool use_lazy1 = (p.match_strategy == 1);

    const size_t src_size = src.size();
    const uint8_t* s = src.data();
    if (counters) {
        counters->calls++;
        counters->src_bytes += (uint64_t)src_size;
    }

    auto accumulate_non_io_counters = [&](const GlobalChainLzCounters& sc) {
        if (!counters) return;
        counters->match_count += sc.match_count;
        counters->match_bytes += sc.match_bytes;
        counters->literal_bytes += sc.literal_bytes;
        counters->chain_steps += sc.chain_steps;
        counters->depth_limit_hits += sc.depth_limit_hits;
        counters->early_maxlen_hits += sc.early_maxlen_hits;
        counters->nice_cutoff_hits += sc.nice_cutoff_hits;
        counters->len3_reject_dist += sc.len3_reject_dist;
        counters->optparse_enabled += sc.optparse_enabled;
        counters->optparse_fallback_count += sc.optparse_fallback_count;
        counters->optparse_fallback_memcap += sc.optparse_fallback_memcap;
        counters->optparse_fallback_allocfail += sc.optparse_fallback_allocfail;
        counters->optparse_fallback_unreachable += sc.optparse_fallback_unreachable;
        counters->optparse_dp_positions += sc.optparse_dp_positions;
        counters->optparse_lit_edges_eval += sc.optparse_lit_edges_eval;
        counters->optparse_match_edges_eval += sc.optparse_match_edges_eval;
        counters->optparse_tokens_litrun += sc.optparse_tokens_litrun;
        counters->optparse_tokens_match += sc.optparse_tokens_match;
        counters->optparse_chose_shorter_than_longest +=
            sc.optparse_chose_shorter_than_longest;
        counters->optparse_probe_accept += sc.optparse_probe_accept;
        counters->optparse_probe_reject += sc.optparse_probe_reject;
        counters->optparse_adopt += sc.optparse_adopt;
        counters->optparse_reject_small_gain += sc.optparse_reject_small_gain;
    };

    if (p.match_strategy == 2) {
        GlobalChainLzParams lazy_params = p;
        lazy_params.match_strategy = 1;

        GlobalChainLzCounters lazy_counters{};
        std::vector<uint8_t> lazy_out =
            compress_global_chain_lz(src, lazy_params, &lazy_counters);

        const uint64_t ratio_x1000 =
            (src_size == 0) ? 1000ull : ((uint64_t)lazy_out.size() * 1000ull) / (uint64_t)src_size;
        const bool probe_pass =
            ((int)src_size <= p.opt_probe_src_max_bytes) &&
            ((int)ratio_x1000 >= p.opt_probe_ratio_min_x1000) &&
            ((int)ratio_x1000 <= p.opt_probe_ratio_max_x1000);
        if (!probe_pass) {
            if (counters) counters->optparse_probe_reject++;
            lazy_counters.calls = 0;
            lazy_counters.src_bytes = 0;
            lazy_counters.out_bytes = 0;
            accumulate_non_io_counters(lazy_counters);
            if (counters) counters->out_bytes += (uint64_t)lazy_out.size();
            return lazy_out;
        }

        if (counters) {
            counters->optparse_probe_accept++;
            counters->optparse_enabled++;
        }

        GlobalChainLzCounters opt_counters{};
        std::vector<uint8_t> opt_out;
        if (compress_global_chain_lz_optparse(src, p, &opt_counters, &opt_out)) {
            if (opt_out.size() + (size_t)std::max(0, p.opt_min_gain_bytes) <= lazy_out.size()) {
                if (counters) counters->optparse_adopt++;
                opt_counters.calls = 0;
                opt_counters.src_bytes = 0;
                opt_counters.out_bytes = 0;
                accumulate_non_io_counters(opt_counters);
                if (counters) counters->out_bytes += (uint64_t)opt_out.size();
                return opt_out;
            }
            if (counters) counters->optparse_reject_small_gain++;
        } else {
            opt_counters.calls = 0;
            opt_counters.src_bytes = 0;
            opt_counters.out_bytes = 0;
            accumulate_non_io_counters(opt_counters);
        }

        lazy_counters.calls = 0;
        lazy_counters.src_bytes = 0;
        lazy_counters.out_bytes = 0;
        accumulate_non_io_counters(lazy_counters);
        if (counters) counters->out_bytes += (uint64_t)lazy_out.size();
        return lazy_out;
    }

    std::vector<uint8_t> out;
    const size_t worst_lit_chunks = (src_size + 254) / 255;
    out.reserve(src_size + (worst_lit_chunks * 2) + 64);

    thread_local std::array<int, HASH_SIZE> head{};
    thread_local std::array<uint32_t, HASH_SIZE> head_epoch{};
    thread_local uint32_t epoch = 1;
    epoch++;
    if (epoch == 0) {
        head_epoch.fill(0);
        epoch = 1;
    }

    auto head_get = [&](uint32_t h) -> int {
        return (head_epoch[h] == epoch) ? head[h] : -1;
    };
    auto head_set = [&](uint32_t h, int pos) {
        head_epoch[h] = epoch;
        head[h] = pos;
    };

    thread_local std::vector<int> prev;
    if (prev.size() < src_size) prev.resize(src_size);

    auto hash3 = [&](size_t p_pos) -> uint32_t {
        uint32_t v = ((uint32_t)s[p_pos] << 16) |
                     ((uint32_t)s[p_pos + 1] << 8) |
                     (uint32_t)s[p_pos + 2];
        return (v * 0x1e35a7bdu) >> (32 - HASH_BITS);
    };

    auto flush_literals = [&](size_t start, size_t end) {
        size_t cur = start;
        while (cur < end) {
            size_t chunk = std::min<size_t>(255, end - cur);
            const size_t old_size = out.size();
            out.resize(old_size + 2 + chunk);
            uint8_t* dst = out.data() + old_size;
            dst[0] = 0; // LITRUN
            dst[1] = (uint8_t)chunk;
            std::memcpy(dst + 2, s + cur, chunk);
            if (counters) counters->literal_bytes += (uint64_t)chunk;
            cur += chunk;
        }
    };

    auto match_len_from = [&](size_t ref_pos, size_t cur_pos) -> int {
        const size_t max_len = std::min<size_t>(255, src_size - cur_pos);
        int len = 3;

        const uint8_t* a = s + ref_pos + 3;
        const uint8_t* b = s + cur_pos + 3;
        size_t remain = max_len - 3;
        while (remain >= sizeof(uint64_t)) {
            uint64_t va = 0;
            uint64_t vb = 0;
            std::memcpy(&va, a, sizeof(uint64_t));
            std::memcpy(&vb, b, sizeof(uint64_t));
            if (va != vb) {
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
                uint64_t diff = va ^ vb;
#if defined(__GNUC__) || defined(__clang__)
                const int common = (int)(__builtin_ctzll(diff) >> 3);
#else
                int common = 0;
                while ((diff & 0xFFu) == 0u && common < 8) {
                    diff >>= 8;
                    common++;
                }
#endif
                len += common;
                return len;
#else
                int common = 0;
                while (common < 8 && a[common] == b[common]) common++;
                len += common;
                return len;
#endif
            }
            a += sizeof(uint64_t);
            b += sizeof(uint64_t);
            len += (int)sizeof(uint64_t);
            remain -= sizeof(uint64_t);
        }
        while (remain > 0 && *a == *b) {
            ++a;
            ++b;
            ++len;
            --remain;
        }
        return len;
    };

    struct MatchSearchResult {
        int len = 0;
        int dist = 0;
        bool depth_limit_hit = false;
        bool early_maxlen_hit = false;
        bool nice_cutoff_hit = false;
    };

    auto find_best_match = [&](size_t cur_pos) -> MatchSearchResult {
        MatchSearchResult result{};
        if (cur_pos + 2 >= src_size) return result;
        int ref = head_get(hash3(cur_pos));
        int depth = 0;
        while (ref >= 0 && depth < chain_depth) {
            if (counters) counters->chain_steps++;
            const size_t ref_pos = (size_t)ref;
            const int dist = (int)(cur_pos - ref_pos);
            if (dist > 0 && dist <= window_size) {
                if (s[ref_pos] == s[cur_pos] &&
                    s[ref_pos + 1] == s[cur_pos + 1] &&
                    s[ref_pos + 2] == s[cur_pos + 2]) {
                    int len = 3;
                    if (cur_pos + 3 < src_size && s[ref_pos + 3] == s[cur_pos + 3]) {
                        len = match_len_from(ref_pos, cur_pos);
                    }
                    const bool acceptable = (len >= 4) || (len == 3 && dist <= min_dist_len3);
                    if (!acceptable && len == 3 && dist > min_dist_len3 && counters) {
                        counters->len3_reject_dist++;
                    }
                    if (acceptable && (len > result.len || (len == result.len && dist < result.dist))) {
                        result.len = len;
                        result.dist = dist;
                        if (result.len == 255) {
                            result.early_maxlen_hit = true;
                            break;
                        }
                        if (result.len >= nice_length) {
                            result.nice_cutoff_hit = true;
                            break;
                        }
                    }
                }
            } else if (dist > window_size) {
                break;
            }
            ref = prev[ref_pos];
            depth++;
        }
        if (!result.early_maxlen_hit && ref >= 0 && depth >= chain_depth) {
            result.depth_limit_hit = true;
        }
        if (counters) {
            if (result.depth_limit_hit) counters->depth_limit_hits++;
            if (result.early_maxlen_hit) counters->early_maxlen_hits++;
            if (result.nice_cutoff_hit) counters->nice_cutoff_hits++;
        }
        return result;
    };

    size_t pos = 0;
    size_t lit_start = 0;
    while (pos + 2 < src_size) {
        const uint32_t h = hash3(pos);
        const MatchSearchResult best = find_best_match(pos);

        prev[pos] = head_get(h);
        head_set(h, (int)pos);

        bool defer_to_next = false;
        if (use_lazy1 && best.len > 0 && pos + 3 < src_size) {
            const MatchSearchResult next = find_best_match(pos + 1);
            if (next.len > best.len ||
                (next.len == best.len && next.len > 0 && next.dist < best.dist)) {
                defer_to_next = true;
            }
        }

        if (best.len > 0 && !defer_to_next) {
            flush_literals(lit_start, pos);
            const size_t out_pos = out.size();
            out.resize(out_pos + 4);
            uint8_t* dst = out.data() + out_pos;
            dst[0] = 1; // MATCH
            dst[1] = (uint8_t)best.len;
            dst[2] = (uint8_t)(best.dist & 0xFF);
            dst[3] = (uint8_t)((best.dist >> 8) & 0xFF);
            if (counters) {
                counters->match_count++;
                counters->match_bytes += (uint64_t)best.len;
            }

            for (int i = 1; i < best.len && pos + (size_t)i + 2 < src_size; i++) {
                size_t p = pos + (size_t)i;
                uint32_t h2 = hash3(p);
                prev[p] = head_get(h2);
                head_set(h2, (int)p);
            }

            pos += (size_t)best.len;
            lit_start = pos;
        } else {
            pos++;
        }
    }

    flush_literals(lit_start, src_size);
    if (counters) counters->out_bytes += (uint64_t)out.size();
    return out;
}

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn>
inline std::vector<uint8_t> build_mode0_payload(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val, EncodeSharedLzFn&& encode_byte_stream_shared_lz
) {
    std::vector<uint8_t> row_pred_ids(pad_h, 0);
    std::vector<uint8_t> residual_bytes;
    residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = residual_bytes.data();

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int up = up_row ? (int)up_row[x] : 0;
            cost0 += (uint64_t)std::abs(cur);
            cost1 += (uint64_t)std::abs(cur - up);
            cost2 += (uint64_t)std::abs(cur - (up / 2));
        }

        int best_p = 0;
        uint64_t best_cost = cost0;
        if (cost1 < best_cost) {
            best_cost = cost1;
            best_p = 1;
        }
        if (cost2 < best_cost) {
            best_p = 2;
        }
        row_pred_ids[y] = (uint8_t)best_p;

        if (best_p == 0) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t left = (x > 0) ? row[x - 1] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)left);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else if (best_p == 1) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t up = up_row ? up_row[x] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)up);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t left = (x > 0) ? row[x - 1] : 0;
                const int16_t up = up_row ? up_row[x] : 0;
                const int16_t pred = (int16_t)(((int)left + (int)up) / 2);
                const int16_t resid = (int16_t)((int)row[x] - (int)pred);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        }
    }

    auto resid_lz = TileLZ::compress(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    // [magic][mode=0][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4][pred_ids][payload]
    std::vector<uint8_t> out;
    out.reserve(18 + row_pred_ids.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(0);
    uint32_t pred_count = pad_h;
    uint32_t resid_raw_count = (uint32_t)residual_bytes.size();
    uint32_t resid_payload_size = (uint32_t)resid_lz_rans.size();
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pred_count);
    push_u32(resid_raw_count);
    push_u32(resid_payload_size);
    out.insert(out.end(), row_pred_ids.begin(), row_pred_ids.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

struct Mode1Prepared {
    std::vector<uint8_t> row_pred_ids;
    std::vector<int16_t> residuals;
    std::vector<uint8_t> residual_bytes;
};

struct PackedPredictorStream {
    uint8_t mode = 0; // 0=raw, 1=rANS
    std::vector<uint8_t> payload;
    bool valid = false;
};

inline size_t mode12_min_candidate_size(const PackedPredictorStream& packed_pred) {
    if (!packed_pred.valid || packed_pred.payload.empty()) {
        return std::numeric_limits<size_t>::max();
    }
    // mode1/mode2 wrapper fixed header (27 bytes) + pred payload + residual payload (>=1 byte)
    return 27u + packed_pred.payload.size() + 1u;
}

template <typename EncodeByteStreamFn>
inline PackedPredictorStream build_packed_predictor_stream(
    const std::vector<uint8_t>& row_pred_ids,
    EncodeByteStreamFn&& encode_byte_stream
) {
    PackedPredictorStream out;
    if (row_pred_ids.empty()) return out;

    out.payload = row_pred_ids;
    auto pred_rans = encode_byte_stream(row_pred_ids);
    if (!pred_rans.empty() && pred_rans.size() < out.payload.size()) {
        out.payload = std::move(pred_rans);
        out.mode = 1;
    }
    out.valid = true;
    return out;
}

template <typename ZigzagEncodeFn>
inline Mode1Prepared build_mode1_prepared(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val
) {
    Mode1Prepared prepared;
    prepared.row_pred_ids.resize(pad_h, 0);
    prepared.residuals.resize(pixel_count);
    prepared.residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = prepared.residual_bytes.data();

    std::vector<int16_t> recon(pixel_count, 0);

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        uint64_t cost3 = 0; // PAETH
        uint64_t cost4 = 0; // MED
        uint64_t cost5 = 0; // WEIGHTED_A
        uint64_t cost6 = 0; // WEIGHTED_B
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int16_t b = up_row ? up_row[x] : 0;
            const int16_t c = (up_row && x > 0) ? up_row[x - 1] : 0;
            const int16_t a = (x > 0) ? row[x - 1] : 0;
            const int pred2 = ((int)a + (int)b) / 2;
            const int pred3 = (int)LosslessFilter::paeth_predictor(a, b, c);
            const int pred4 = (int)LosslessFilter::med_predictor(a, b, c);
            const int pred5 = ((int)a * 3 + (int)b) / 4;
            const int pred6 = ((int)a + (int)b * 3) / 4;
            cost0 += (uint64_t)std::abs(cur - (int)a);
            cost1 += (uint64_t)std::abs(cur - (int)b);
            cost2 += (uint64_t)std::abs(cur - pred2);
            cost3 += (uint64_t)std::abs(cur - pred3);
            cost4 += (uint64_t)std::abs(cur - pred4);
            cost5 += (uint64_t)std::abs(cur - pred5);
            cost6 += (uint64_t)std::abs(cur - pred6);
        }

        int best_p = 0;
        uint64_t best_cost = cost0;
        if (cost1 < best_cost) {
            best_cost = cost1;
            best_p = 1;
        }
        if (cost2 < best_cost) {
            best_cost = cost2;
            best_p = 2;
        }
        if (cost3 < best_cost) {
            best_cost = cost3;
            best_p = 3;
        }
        if (cost4 < best_cost) {
            best_cost = cost4;
            best_p = 4;
        }
        if (cost5 < best_cost) {
            best_cost = cost5;
            best_p = 5;
        }
        if (cost6 < best_cost) {
            best_p = 6;
        }
        prepared.row_pred_ids[y] = (uint8_t)best_p;

        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (best_p == 0) pred = a;
            else if (best_p == 1) pred = b;
            else if (best_p == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (best_p == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else if (best_p == 4) pred = LosslessFilter::med_predictor(a, b, c);
            else if (best_p == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
            else pred = (int16_t)(((int)a + (int)b * 3) / 4);

            int16_t cur = row[x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);
            prepared.residuals[(size_t)y * pad_w + x] = resid;

            uint16_t zz = zigzag_encode_val(resid);
            resid_dst[0] = (uint8_t)(zz & 0xFF);
            resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
            resid_dst += 2;
        }
    }
    return prepared;
}

template <typename Mode1PreparedT,
          typename EncodeSharedLzFn,
          typename CompressResidualFn>
inline std::vector<uint8_t> build_mode1_payload_from_prepared(
    const Mode1PreparedT& prepared,
    const PackedPredictorStream& packed_pred,
    uint32_t pad_h,
    uint32_t pixel_count,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    uint8_t out_mode,
    CompressResidualFn&& compress_residual
) {
    const auto& residual_bytes = prepared.residual_bytes;
    if (!packed_pred.valid || packed_pred.payload.empty() || residual_bytes.empty()) return {};

    auto resid_lz = compress_residual(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    // [magic][mode=1/2][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][resid_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + packed_pred.payload.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(out_mode);
    uint32_t pred_count = pad_h;
    uint32_t resid_raw_count = (uint32_t)residual_bytes.size();
    uint32_t resid_payload_size = (uint32_t)resid_lz_rans.size();
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pred_count);
    push_u32(resid_raw_count);
    push_u32(resid_payload_size);
    out.push_back(packed_pred.mode);
    push_u32(pred_count);
    push_u32((uint32_t)packed_pred.payload.size());
    out.insert(out.end(), packed_pred.payload.begin(), packed_pred.payload.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

template <typename Mode1PreparedT,
          typename PackedPredictorStreamT,
          typename EncodeByteStreamFn>
inline std::vector<uint8_t> build_mode3_payload_from_prepared(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    const Mode1PreparedT& prepared,
    const PackedPredictorStreamT& packed_pred,
    EncodeByteStreamFn&& encode_byte_stream
) {
    if (!packed_pred.valid || packed_pred.payload.empty()) return {};

    std::vector<uint8_t> flat_bytes;
    std::vector<uint8_t> edge_bytes;
    flat_bytes.reserve((size_t)pixel_count * 2);
    edge_bytes.reserve((size_t)pixel_count * 2);

    const std::vector<uint8_t>& pred_ids = prepared.row_pred_ids;
    std::vector<int16_t> recon(pixel_count, 0);

    for (uint32_t y = 0; y < pad_h; y++) {
        uint8_t pid = pred_ids[y];
        const int16_t* padded_row = padded + (size_t)y * pad_w;
        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (pid == 0) pred = a;
            else if (pid == 1) pred = b;
            else if (pid == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (pid == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else if (pid == 4) pred = LosslessFilter::med_predictor(a, b, c);
            else if (pid == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
            else if (pid == 6) pred = (int16_t)(((int)a + (int)b * 3) / 4);

            int16_t cur = padded_row[x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);

            uint16_t zz = zigzag_encode_val(resid);
            
            int grad = std::max(std::abs(a - c), std::abs(b - c));
            if (grad < 16) {
                flat_bytes.push_back((uint8_t)(zz & 0xFF));
                flat_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
            } else {
                edge_bytes.push_back((uint8_t)(zz & 0xFF));
                edge_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
            }
        }
    }

    auto flat_rans = encode_byte_stream(flat_bytes);
    auto edge_rans = encode_byte_stream(edge_bytes);

    // [magic][mode=3][pixel_count:4][pred_count:4][flat_payload_size:4][edge_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][flat_payload][edge_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + packed_pred.payload.size() + flat_rans.size() + edge_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(3);
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pad_h); // pred_count
    push_u32((uint32_t)flat_rans.size());
    push_u32((uint32_t)edge_rans.size());
    out.push_back(packed_pred.mode);
    push_u32(pad_h); // pred_raw_count
    push_u32((uint32_t)packed_pred.payload.size());
    out.insert(out.end(), packed_pred.payload.begin(), packed_pred.payload.end());
    out.insert(out.end(), flat_rans.begin(), flat_rans.end());
    out.insert(out.end(), edge_rans.begin(), edge_rans.end());
    return out;
}

} // namespace detail

// Natural/photo-oriented route:
// mode0: row SUB/UP/AVG + residual LZ+rANS(shared CDF)
// mode1: row SUB/UP/AVG/PAETH/MED + compressed predictor stream
// mode2: mode1 predictor set + natural-only global-chain LZ for residual stream
// mode3: mode1 predictor set + 2-context adaptive rANS (flat/edge)
template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile_padded(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr,
    int mode2_nice_length_override = -1,
    int mode2_match_strategy_override = -1
) {
    using Clock = std::chrono::steady_clock;
    auto ns_since = [](const Clock::time_point& t0, const Clock::time_point& t1) -> uint64_t {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };
    if (!padded || pad_w == 0 || pad_h == 0) return {};
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) return {};

    auto lz_params = detail::global_chain_lz_runtime_params();
    if (mode2_nice_length_override >= 4 && mode2_nice_length_override <= 255) {
        lz_params.nice_length = mode2_nice_length_override;
    }
    if (mode2_match_strategy_override >= 0 && mode2_match_strategy_override <= 2) {
        lz_params.match_strategy = mode2_match_strategy_override;
    }
    std::vector<uint8_t> mode0;
    std::vector<uint8_t> mode1;
    std::vector<uint8_t> mode2;
    std::vector<uint8_t> mode3;
    detail::Mode1Prepared mode1_prepared;
    detail::PackedPredictorStream mode1_pred;
    auto accumulate_mode2_lz = [&](const detail::GlobalChainLzCounters& c) {
        if (!stats) return;
        stats->natural_row_mode2_lz_calls += c.calls;
        stats->natural_row_mode2_lz_src_bytes_sum += c.src_bytes;
        stats->natural_row_mode2_lz_out_bytes_sum += c.out_bytes;
        stats->natural_row_mode2_lz_match_count += c.match_count;
        stats->natural_row_mode2_lz_match_bytes_sum += c.match_bytes;
        stats->natural_row_mode2_lz_literal_bytes_sum += c.literal_bytes;
        stats->natural_row_mode2_lz_chain_steps_sum += c.chain_steps;
        stats->natural_row_mode2_lz_depth_limit_hits += c.depth_limit_hits;
        stats->natural_row_mode2_lz_early_maxlen_hits += c.early_maxlen_hits;
        stats->natural_row_mode2_lz_nice_cutoff_hits += c.nice_cutoff_hits;
        stats->natural_row_mode2_lz_len3_reject_dist += c.len3_reject_dist;
        stats->natural_row_mode2_lz_optparse_enabled += c.optparse_enabled;
        stats->natural_row_mode2_lz_optparse_fallback_count += c.optparse_fallback_count;
        stats->natural_row_mode2_lz_optparse_fallback_memcap += c.optparse_fallback_memcap;
        stats->natural_row_mode2_lz_optparse_fallback_allocfail += c.optparse_fallback_allocfail;
        stats->natural_row_mode2_lz_optparse_fallback_unreachable +=
            c.optparse_fallback_unreachable;
        stats->natural_row_mode2_lz_optparse_dp_positions_sum += c.optparse_dp_positions;
        stats->natural_row_mode2_lz_optparse_lit_edges_sum += c.optparse_lit_edges_eval;
        stats->natural_row_mode2_lz_optparse_match_edges_sum += c.optparse_match_edges_eval;
        stats->natural_row_mode2_lz_optparse_tokens_lit_sum += c.optparse_tokens_litrun;
        stats->natural_row_mode2_lz_optparse_tokens_match_sum += c.optparse_tokens_match;
        stats->natural_row_mode2_lz_optparse_shorter_than_longest_sum +=
            c.optparse_chose_shorter_than_longest;
        stats->natural_row_mode2_lz_optparse_probe_accept += c.optparse_probe_accept;
        stats->natural_row_mode2_lz_optparse_probe_reject += c.optparse_probe_reject;
        stats->natural_row_mode2_lz_optparse_adopt += c.optparse_adopt;
        stats->natural_row_mode2_lz_optparse_reject_small_gain +=
            c.optparse_reject_small_gain;
    };
    constexpr uint32_t kPrepParallelPixelThreshold = 262144u;
    thread_budget::ScopedThreadTokens pipeline_tokens;
    if (pixel_count >= kPrepParallelPixelThreshold) {
        pipeline_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(1);
    }

    if (pipeline_tokens.acquired()) {
        struct ReadyData {
            std::shared_ptr<const detail::Mode1Prepared> prepared;
            std::shared_ptr<const detail::PackedPredictorStream> pred;
            uint64_t prep_ns = 0;
            uint64_t pred_ns = 0;
        };
        struct TimedPayload {
            std::vector<uint8_t> payload;
            uint64_t elapsed_ns = 0;
            detail::GlobalChainLzCounters lz;
        };
        struct TimedMode23 {
            TimedPayload mode2;
            std::vector<uint8_t> mode3;
            uint64_t mode3_elapsed_ns = 0;
        };
        if (stats) {
            stats->natural_row_prep_parallel_count++;
            stats->natural_row_prep_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
            stats->natural_row_mode12_parallel_count++;
            stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
        }

        std::promise<ReadyData> ready_promise;
        auto ready_future = ready_promise.get_future();
        auto mode23_future = std::async(
            std::launch::async,
            [&,
             rp = std::move(ready_promise)]() mutable -> TimedMode23 {
                thread_budget::ScopedParallelRegion guard;
                ReadyData ready;
                const auto t_prep0 = Clock::now();
                auto prep_local = detail::build_mode1_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    zigzag_encode_val
                );
                const auto t_prep1 = Clock::now();
                ready.prep_ns = ns_since(t_prep0, t_prep1);

                const auto t_pred0 = Clock::now();
                auto pred_local = detail::build_packed_predictor_stream(
                    prep_local.row_pred_ids,
                    encode_byte_stream
                );
                const auto t_pred1 = Clock::now();
                ready.pred_ns = ns_since(t_pred0, t_pred1);
                ready.prepared = std::make_shared<const detail::Mode1Prepared>(std::move(prep_local));
                ready.pred = std::make_shared<const detail::PackedPredictorStream>(std::move(pred_local));

                rp.set_value(ready);

                const auto t_mode2_0 = Clock::now();
                TimedPayload out2;
                detail::GlobalChainLzCounters lz_counters;
                out2.payload = detail::build_mode1_payload_from_prepared(
                    *ready.prepared,
                    *ready.pred,
                    pad_h,
                    pixel_count,
                    encode_byte_stream_shared_lz,
                    2,
                    [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                        return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                    }
                );
                out2.lz = lz_counters;
                const auto t_mode2_1 = Clock::now();
                out2.elapsed_ns = ns_since(t_mode2_0, t_mode2_1);

                const auto t_mode3_0 = Clock::now();
                std::vector<uint8_t> out3 = detail::build_mode3_payload_from_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    *ready.prepared,
                    *ready.pred,
                    encode_byte_stream
                );
                const auto t_mode3_1 = Clock::now();

                return TimedMode23{
                    std::move(out2),
                    std::move(out3),
                    ns_since(t_mode3_0, t_mode3_1)
                };
            }
        );

        const auto t_mode0_0 = Clock::now();
        mode0 = detail::build_mode0_payload(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val, encode_byte_stream_shared_lz
        );
        const auto t_mode0_1 = Clock::now();
        if (stats) stats->natural_row_mode0_build_ns += ns_since(t_mode0_0, t_mode0_1);

        auto ready = ready_future.get();
        if (stats) {
            stats->natural_row_mode1_prepare_ns += ready.prep_ns;
            stats->natural_row_pred_pack_ns += ready.pred_ns;
            if (ready.pred->mode == 0) stats->natural_row_pred_mode_raw_count++;
            else stats->natural_row_pred_mode_rans_count++;
        }

        const auto t_mode1_0 = Clock::now();
        mode1 = detail::build_mode1_payload_from_prepared(
            *ready.prepared,
            *ready.pred,
            pad_h,
            pixel_count,
            encode_byte_stream_shared_lz,
            1,
            [](const std::vector<uint8_t>& bytes) {
                return TileLZ::compress(bytes);
            }
        );
        const auto t_mode1_1 = Clock::now();
        if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);

        auto mode23_res = mode23_future.get();
        mode2 = std::move(mode23_res.mode2.payload);
        mode3 = std::move(mode23_res.mode3);
        if (stats) {
            stats->natural_row_mode2_build_ns += mode23_res.mode2.elapsed_ns;
            stats->natural_row_mode3_build_ns += mode23_res.mode3_elapsed_ns;
        }
        accumulate_mode2_lz(mode23_res.mode2.lz);
    } else {
        if (stats) stats->natural_row_prep_seq_count++;
        const auto t_mode0_0 = Clock::now();
        mode0 = detail::build_mode0_payload(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val, encode_byte_stream_shared_lz
        );
        const auto t_mode0_1 = Clock::now();
        if (stats) stats->natural_row_mode0_build_ns += ns_since(t_mode0_0, t_mode0_1);
        const auto t_mode1p_0 = Clock::now();
        mode1_prepared = detail::build_mode1_prepared(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val
        );
        const auto t_mode1p_1 = Clock::now();
        if (stats) stats->natural_row_mode1_prepare_ns += ns_since(t_mode1p_0, t_mode1p_1);
        const auto t_pred0 = Clock::now();
        mode1_pred = detail::build_packed_predictor_stream(
            mode1_prepared.row_pred_ids,
            encode_byte_stream
        );
        const auto t_pred1 = Clock::now();
        if (stats) {
            stats->natural_row_pred_pack_ns += ns_since(t_pred0, t_pred1);
            if (mode1_pred.mode == 0) stats->natural_row_pred_mode_raw_count++;
            else stats->natural_row_pred_mode_rans_count++;
        }

        const size_t mode2_min_size = detail::mode12_min_candidate_size(mode1_pred);
        const uint64_t mode2_limit_vs_mode0 =
            ((uint64_t)mode0.size() * (uint64_t)lz_params.bias_permille) / 1000ull;
        const bool mode2_possible_vs_mode0 =
            (mode2_min_size != std::numeric_limits<size_t>::max()) &&
            (mode2_min_size <= mode2_limit_vs_mode0);

        constexpr uint32_t kMode12ParallelPixelThreshold = 262144u;
        thread_budget::ScopedThreadTokens mode12_tokens;
        if (pixel_count >= kMode12ParallelPixelThreshold) {
            mode12_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(1);
        }
        struct TimedPayload {
            std::vector<uint8_t> payload;
            uint64_t elapsed_ns = 0;
            detail::GlobalChainLzCounters lz;
        };
        struct TimedMode23 {
            TimedPayload mode2;
            std::vector<uint8_t> mode3;
            uint64_t mode3_elapsed_ns = 0;
        };
        if (mode12_tokens.acquired() && mode2_possible_vs_mode0) {
            if (stats) {
                stats->natural_row_mode12_parallel_count++;
                stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)mode12_tokens.count();
            }
            auto f_mode23 = std::async(std::launch::async, [&]() -> TimedMode23 {
                thread_budget::ScopedParallelRegion guard;
                const auto t0 = Clock::now();
                TimedPayload out2;
                detail::GlobalChainLzCounters lz_counters;
                out2.payload = detail::build_mode1_payload_from_prepared(
                    mode1_prepared,
                    mode1_pred,
                    pad_h,
                    pixel_count,
                    encode_byte_stream_shared_lz,
                    2,
                    [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                        return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                    }
                );
                out2.lz = lz_counters;
                const auto t1 = Clock::now();
                out2.elapsed_ns = ns_since(t0, t1);

                const auto t_mode3_0 = Clock::now();
                std::vector<uint8_t> out3 = detail::build_mode3_payload_from_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    mode1_prepared,
                    mode1_pred,
                    encode_byte_stream
                );
                const auto t_mode3_1 = Clock::now();
                return TimedMode23{
                    std::move(out2),
                    std::move(out3),
                    ns_since(t_mode3_0, t_mode3_1)
                };
            });
            const auto t_mode1_0 = Clock::now();
            mode1 = detail::build_mode1_payload_from_prepared(
                mode1_prepared,
                mode1_pred,
                pad_h,
                pixel_count,
                encode_byte_stream_shared_lz,
                1,
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                }
            );
            const auto t_mode1_1 = Clock::now();
            if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);
            auto mode23_res = f_mode23.get();
            mode2 = std::move(mode23_res.mode2.payload);
            mode3 = std::move(mode23_res.mode3);
            if (stats) {
                stats->natural_row_mode2_build_ns += mode23_res.mode2.elapsed_ns;
                stats->natural_row_mode3_build_ns += mode23_res.mode3_elapsed_ns;
            }
            accumulate_mode2_lz(mode23_res.mode2.lz);
            if (stats && mode2.empty()) stats->natural_row_mode2_bias_reject_count++;
        } else {
            if (stats) stats->natural_row_mode12_seq_count++;
            const auto t_mode1_0 = Clock::now();
            mode1 = detail::build_mode1_payload_from_prepared(
                mode1_prepared,
                mode1_pred,
                pad_h,
                pixel_count,
                encode_byte_stream_shared_lz,
                1,
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                }
            );
            const auto t_mode1_1 = Clock::now();
            if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);
            if (mode2_possible_vs_mode0) {
                const uint64_t best_after_mode1 =
                    std::min<uint64_t>((uint64_t)mode0.size(), (uint64_t)mode1.size());
                const uint64_t mode2_limit_vs_best =
                    (best_after_mode1 * (uint64_t)lz_params.bias_permille) / 1000ull;
                const bool mode2_possible_vs_best =
                    (mode2_min_size <= mode2_limit_vs_best);
                if (mode2_possible_vs_best) {
                    const auto t_mode2_0 = Clock::now();
                    detail::GlobalChainLzCounters lz_counters;
                    mode2 = detail::build_mode1_payload_from_prepared(
                        mode1_prepared,
                        mode1_pred,
                        pad_h,
                        pixel_count,
                        encode_byte_stream_shared_lz,
                        2,
                        [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                            return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                        }
                    );
                    const auto t_mode2_1 = Clock::now();
                    if (stats) stats->natural_row_mode2_build_ns += ns_since(t_mode2_0, t_mode2_1);
                    accumulate_mode2_lz(lz_counters);
                    if (stats && mode2.empty()) stats->natural_row_mode2_bias_reject_count++;
                } else if (stats) {
                    stats->natural_row_mode2_bias_reject_count++;
                }
            } else if (stats) {
                stats->natural_row_mode2_bias_reject_count++;
            }
            const auto t_mode3_0 = Clock::now();
            mode3 = detail::build_mode3_payload_from_prepared(
                padded, pad_w, pad_h, pixel_count,
                mode1_prepared,
                mode1_pred,
                encode_byte_stream
            );
            const auto t_mode3_1 = Clock::now();
            if (stats) stats->natural_row_mode3_build_ns += ns_since(t_mode3_0, t_mode3_1);
        }
    }
    if (mode0.empty()) return {};
    if (stats) {
        stats->natural_row_mode0_size_sum += (uint64_t)mode0.size();
        stats->natural_row_mode1_size_sum += (uint64_t)mode1.size();
        stats->natural_row_mode2_size_sum += (uint64_t)mode2.size();
        stats->natural_row_mode3_size_sum += (uint64_t)mode3.size();
    }

    uint8_t selected_mode = 0;
    std::vector<uint8_t> best = std::move(mode0);
    if (!mode1.empty() && mode1.size() < best.size()) {
        best = std::move(mode1);
        selected_mode = 1;
    }
    if (!mode2.empty()) {
        const uint64_t lhs = (uint64_t)mode2.size() * 1000ull;
        const uint64_t rhs = (uint64_t)best.size() * (uint64_t)lz_params.bias_permille;
        if (lhs <= rhs) {
            best = std::move(mode2);
            selected_mode = 2;
            if (stats) stats->natural_row_mode2_bias_adopt_count++;
        } else if (stats) {
            stats->natural_row_mode2_bias_reject_count++;
        }
    }
    if (!mode3.empty() && mode3.size() < best.size()) {
        best = std::move(mode3);
        selected_mode = 3;
    }
    if (stats) {
        if (selected_mode == 0) stats->natural_row_mode0_selected_count++;
        else if (selected_mode == 1) stats->natural_row_mode1_selected_count++;
        else if (selected_mode == 2) stats->natural_row_mode2_selected_count++;
        else stats->natural_row_mode3_selected_count++;
    }
    return best;
}

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr,
    int mode2_nice_length_override = -1,
    int mode2_match_strategy_override = -1
) {
    if (!plane || width == 0 || height == 0) return {};
    const uint32_t pad_w = ((width + 7) / 8) * 8;
    const uint32_t pad_h = ((height + 7) / 8) * 8;
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) return {};

    std::vector<int16_t> padded(pixel_count, 0);
    for (uint32_t y = 0; y < pad_h; y++) {
        uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < pad_w; x++) {
            uint32_t sx = std::min(x, width - 1);
            padded[(size_t)y * pad_w + x] = plane[(size_t)sy * width + sx];
        }
    }

    return encode_plane_lossless_natural_row_tile_padded(
        padded.data(), pad_w, pad_h,
        zigzag_encode_val,
        encode_byte_stream_shared_lz,
        encode_byte_stream,
        stats,
        mode2_nice_length_override,
        mode2_match_strategy_override
    );
}

} // namespace hakonyans::lossless_natural_route
