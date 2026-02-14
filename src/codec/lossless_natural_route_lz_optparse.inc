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

