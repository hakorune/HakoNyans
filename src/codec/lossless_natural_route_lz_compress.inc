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
