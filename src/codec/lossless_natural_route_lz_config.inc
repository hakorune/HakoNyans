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

