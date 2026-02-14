#pragma once

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
