#pragma once

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_filter_lo_codec {

// Mode5 runtime parameters (env-configurable)
struct Mode5RuntimeParams {
    int gain_permille;
    int min_raw_bytes;
    int min_lz_bytes;
    int vs_lz_permille;
};

// Mode6 runtime parameters (env-configurable)
struct Mode6RuntimeParams {
    int gain_permille;
    int min_raw_bytes;
    int min_lz_bytes;
    int vs_lz_permille;
};

// Mode7 runtime parameters (env-configurable)
struct Mode7RuntimeParams {
    int gain_permille;
    int min_ctx_bytes;
    int vs_mode4_permille;
};

// Mode8 runtime parameters (env-configurable)
struct Mode8RuntimeParams {
    int gain_permille;
    int min_ctx_bytes;
    int vs_mode4_permille;
};

struct LzProbeRuntimeParams {
    int min_raw_bytes;
    int sample_bytes;
    int threshold_permille;
};

inline int parse_env_int(const char* key, int fallback, int min_v, int max_v) {
    const char* raw = std::getenv(key);
    if (!raw || raw[0] == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0') return fallback;
    if (v < (long)min_v || v > (long)max_v) return fallback;
    return (int)v;
}

inline double parse_env_double(const char* key, double fallback, double min_v, double max_v) {
    const char* raw = std::getenv(key);
    if (!raw || raw[0] == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(raw, &end);
    if (errno != 0 || end == raw || *end != '\0') return fallback;
    if (!(v >= min_v && v <= max_v)) return fallback;
    return v;
}

inline const Mode5RuntimeParams& get_mode5_runtime_params() {
    static const Mode5RuntimeParams params = []() {
        Mode5RuntimeParams p;
        p.gain_permille = parse_env_int("HKN_FILTER_LO_MODE5_GAIN_PERMILLE", 995, 900, 1100);
        p.min_raw_bytes = parse_env_int("HKN_FILTER_LO_MODE5_MIN_RAW_BYTES", 2048, 0, 8192);
        p.min_lz_bytes = parse_env_int("HKN_FILTER_LO_MODE5_MIN_LZ_BYTES", 1024, 0, 4096);
        p.vs_lz_permille = parse_env_int("HKN_FILTER_LO_MODE5_VS_LZ_PERMILLE", 990, 900, 1100);
        return p;
    }();
    return params;
}

inline const Mode6RuntimeParams& get_mode6_runtime_params() {
    static const Mode6RuntimeParams params = []() {
        Mode6RuntimeParams p;
        p.gain_permille = parse_env_int("HKN_FILTER_LO_MODE6_GAIN_PERMILLE", 995, 900, 1100);
        p.min_raw_bytes = parse_env_int("HKN_FILTER_LO_MODE6_MIN_RAW_BYTES", 2048, 0, 8192);
        p.min_lz_bytes = parse_env_int("HKN_FILTER_LO_MODE6_MIN_LZ_BYTES", 1024, 0, 4096);
        p.vs_lz_permille = parse_env_int("HKN_FILTER_LO_MODE6_VS_LZ_PERMILLE", 990, 900, 1100);
        return p;
    }();
    return params;
}

inline bool get_mode6_enable() {
    return parse_env_int("HKN_FILTER_LO_MODE6_ENABLE", 0, 0, 1) != 0;
}

inline const Mode7RuntimeParams& get_mode7_runtime_params() {
    static const Mode7RuntimeParams params = []() {
        Mode7RuntimeParams p;
        p.gain_permille = parse_env_int("HKN_FILTER_LO_MODE7_GAIN_PERMILLE", 990, 900, 1100);
        p.min_ctx_bytes = parse_env_int("HKN_FILTER_LO_MODE7_MIN_CTX_BYTES", 4096, 0, 1 << 20);
        p.vs_mode4_permille = parse_env_int("HKN_FILTER_LO_MODE7_VS_MODE4_PERMILLE", 1000, 900, 1200);
        return p;
    }();
    return params;
}

inline bool get_mode7_enable() {
    return parse_env_int("HKN_FILTER_LO_MODE7_ENABLE", 1, 0, 1) != 0;
}

inline const Mode8RuntimeParams& get_mode8_runtime_params() {
    static const Mode8RuntimeParams params = []() {
        Mode8RuntimeParams p;
        p.gain_permille = parse_env_int("HKN_FILTER_LO_MODE8_GAIN_PERMILLE", 995, 900, 1100);
        p.min_ctx_bytes = parse_env_int("HKN_FILTER_LO_MODE8_MIN_CTX_BYTES", 2048, 0, 8192);
        p.vs_mode4_permille = parse_env_int("HKN_FILTER_LO_MODE8_VS_MODE4_PERMILLE", 1000, 900, 1200);
        return p;
    }();
    return params;
}

inline bool get_mode8_enable() {
    return parse_env_int("HKN_FILTER_LO_MODE8_ENABLE", 0, 0, 1) != 0;
}

inline const LzProbeRuntimeParams& get_lz_probe_runtime_params() {
    static const LzProbeRuntimeParams params = []() {
        LzProbeRuntimeParams p;
        p.min_raw_bytes = parse_env_int("HKN_FILTER_LO_LZ_PROBE_MIN_RAW_BYTES", 4096, 0, 1 << 20);
        p.sample_bytes = parse_env_int("HKN_FILTER_LO_LZ_PROBE_SAMPLE_BYTES", 4096, 256, 1 << 20);
        const int threshold_permille_override = parse_env_int(
            "HKN_FILTER_LO_LZ_PROBE_THRESHOLD_PERMILLE", -1, -1, 2000
        );
        if (threshold_permille_override >= 0) {
            p.threshold_permille = threshold_permille_override;
        } else {
            const double t = parse_env_double("HKN_FILTER_LO_LZ_PROBE_THRESHOLD", 1.03, 0.50, 2.00);
            p.threshold_permille = (int)std::lround(t * 1000.0);
        }
        return p;
    }();
    return params;
}

// Mode6: Parse TileLZ output into token streams for separate entropy coding.
// TileLZ format: [tag=0][len][literals...] for LITRUN, [tag=1][len][dist_lo][dist_hi] for MATCH
// Returns true if parsing succeeded and fills type[], len[], dist[], lit[] streams.
// NOTE: This is the v0x0015 legacy format. For v0x0016 compact format, use parse_tilelz_to_tokens_compact.
inline bool parse_tilelz_to_tokens(
    const std::vector<uint8_t>& lz_bytes,
    std::vector<uint8_t>& type_stream,
    std::vector<uint8_t>& len_stream,
    std::vector<uint8_t>& dist_lo_stream,
    std::vector<uint8_t>& dist_hi_stream,
    std::vector<uint8_t>& lit_stream,
    uint32_t& token_count_out
) {
    type_stream.clear();
    len_stream.clear();
    dist_lo_stream.clear();
    dist_hi_stream.clear();
    lit_stream.clear();
    token_count_out = 0;

    size_t pos = 0;
    const size_t sz = lz_bytes.size();

    while (pos < sz) {
        if (pos >= sz) break;
        uint8_t tag = lz_bytes[pos++];

        if (tag == 0) { // LITRUN
            if (pos >= sz) return false;
            uint8_t len = lz_bytes[pos++];
            if (len == 0) continue;
            if (pos + len > sz) return false;

            type_stream.push_back(0);
            len_stream.push_back(len);
            dist_lo_stream.push_back(0);
            dist_hi_stream.push_back(0);

            lit_stream.insert(lit_stream.end(), lz_bytes.data() + pos, lz_bytes.data() + pos + len);
            pos += len;
            token_count_out++;
        } else if (tag == 1) { // MATCH
            if (pos >= sz) return false;
            uint8_t len = lz_bytes[pos++];
            if (pos + 2 > sz) return false;
            uint8_t dist_lo = lz_bytes[pos++];
            uint8_t dist_hi = lz_bytes[pos++];

            type_stream.push_back(1);
            len_stream.push_back(len);
            dist_lo_stream.push_back(dist_lo);
            dist_hi_stream.push_back(dist_hi);
            token_count_out++;
        } else {
            // Unknown tag
            return false;
        }
    }

    return true;
}

// Mode6 Compact (v0x0016): Parse TileLZ output into token streams.
// DIST is only stored for MATCH tokens (not LITRUN).
// Returns true if parsing succeeded.
// match_count_out: number of MATCH tokens (dist stream size)
inline bool parse_tilelz_to_tokens_compact(
    const std::vector<uint8_t>& lz_bytes,
    std::vector<uint8_t>& type_stream,
    std::vector<uint8_t>& len_stream,
    std::vector<uint8_t>& dist_lo_stream,
    std::vector<uint8_t>& dist_hi_stream,
    std::vector<uint8_t>& lit_stream,
    uint32_t& token_count_out,
    uint32_t& match_count_out
) {
    type_stream.clear();
    len_stream.clear();
    dist_lo_stream.clear();
    dist_hi_stream.clear();
    lit_stream.clear();
    token_count_out = 0;
    match_count_out = 0;

    size_t pos = 0;
    const size_t sz = lz_bytes.size();

    while (pos < sz) {
        if (pos >= sz) break;
        uint8_t tag = lz_bytes[pos++];

        if (tag == 0) { // LITRUN
            if (pos >= sz) return false;
            uint8_t len = lz_bytes[pos++];
            if (len == 0) continue;
            if (pos + len > sz) return false;

            type_stream.push_back(0);
            len_stream.push_back(len);
            // LITRUN has no dist - compact format saves space

            lit_stream.insert(lit_stream.end(), lz_bytes.data() + pos, lz_bytes.data() + pos + len);
            pos += len;
            token_count_out++;
        } else if (tag == 1) { // MATCH
            if (pos >= sz) return false;
            uint8_t len = lz_bytes[pos++];
            if (pos + 2 > sz) return false;
            uint8_t dist_lo = lz_bytes[pos++];
            uint8_t dist_hi = lz_bytes[pos++];

            type_stream.push_back(1);
            len_stream.push_back(len);
            dist_lo_stream.push_back(dist_lo);
            dist_hi_stream.push_back(dist_hi);
            token_count_out++;
            match_count_out++;
        } else {
            // Unknown tag
            return false;
        }
    }

    return true;
}

// Mode6 v0x0017 (type bitpack + len split): Parse TileLZ output into token streams.
// type_bits: packed bits (0=LITRUN, 1=MATCH)
// lit_len: LIT token lengths only
// match_len: MATCH token lengths only
// dist_lo/dist_hi: MATCH distances only
// Returns true if parsing succeeded.
// lit_token_count_out: number of LIT tokens
// match_count_out: number of MATCH tokens
inline bool parse_tilelz_to_tokens_v17(
    const std::vector<uint8_t>& lz_bytes,
    std::vector<uint8_t>& type_bits,
    std::vector<uint8_t>& lit_len,
    std::vector<uint8_t>& match_len,
    std::vector<uint8_t>& dist_lo_stream,
    std::vector<uint8_t>& dist_hi_stream,
    std::vector<uint8_t>& lit_stream,
    uint32_t& token_count_out,
    uint32_t& lit_token_count_out,
    uint32_t& match_count_out
) {
    type_bits.clear();
    lit_len.clear();
    match_len.clear();
    dist_lo_stream.clear();
    dist_hi_stream.clear();
    lit_stream.clear();
    token_count_out = 0;
    lit_token_count_out = 0;
    match_count_out = 0;

    size_t pos = 0;
    const size_t sz = lz_bytes.size();

    while (pos < sz) {
        if (pos >= sz) break;
        uint8_t tag = lz_bytes[pos++];

        if (tag == 0) { // LITRUN
            if (pos >= sz) return false;
            uint8_t len = lz_bytes[pos++];
            if (len == 0) continue;
            if (pos + len > sz) return false;

            // Pack type bit: 0 = LITRUN
            size_t bit_idx = token_count_out;
            size_t byte_idx = bit_idx / 8;
            size_t bit_pos = bit_idx % 8;
            if (byte_idx >= type_bits.size()) {
                type_bits.push_back(0);
            }
            // Bit 0 = LITRUN (no need to set, already 0)

            lit_len.push_back(len);
            lit_stream.insert(lit_stream.end(), lz_bytes.data() + pos, lz_bytes.data() + pos + len);
            pos += len;
            token_count_out++;
            lit_token_count_out++;
        } else if (tag == 1) { // MATCH
            if (pos >= sz) return false;
            uint8_t len = lz_bytes[pos++];
            if (pos + 2 > sz) return false;
            uint8_t dist_lo = lz_bytes[pos++];
            uint8_t dist_hi = lz_bytes[pos++];

            // Pack type bit: 1 = MATCH
            size_t bit_idx = token_count_out;
            size_t byte_idx = bit_idx / 8;
            size_t bit_pos = bit_idx % 8;
            if (byte_idx >= type_bits.size()) {
                type_bits.push_back(0);
            }
            type_bits[byte_idx] |= (1 << bit_pos);

            match_len.push_back(len);
            dist_lo_stream.push_back(dist_lo);
            dist_hi_stream.push_back(dist_hi);
            token_count_out++;
            match_count_out++;
        } else {
            // Unknown tag
            return false;
        }
    }

    return true;
}

// Reconstruct TileLZ byte stream from token streams (for verification/testing).
inline std::vector<uint8_t> reconstruct_tilelz_from_tokens(
    const std::vector<uint8_t>& type_stream,
    const std::vector<uint8_t>& len_stream,
    const std::vector<uint8_t>& dist_lo_stream,
    const std::vector<uint8_t>& dist_hi_stream,
    const std::vector<uint8_t>& lit_stream
) {
    std::vector<uint8_t> out;
    out.reserve(type_stream.size() * 4 + lit_stream.size());

    size_t lit_pos = 0;
    for (size_t i = 0; i < type_stream.size(); i++) {
        uint8_t type = type_stream[i];
        uint8_t len = len_stream[i];

        if (type == 0) { // LITRUN
            out.push_back(0);
            out.push_back(len);
            if (lit_pos + len > lit_stream.size()) {
                out.clear();
                return out; // Error
            }
            out.insert(out.end(), lit_stream.data() + lit_pos, lit_stream.data() + lit_pos + len);
            lit_pos += len;
        } else if (type == 1) { // MATCH
            out.push_back(1);
            out.push_back(len);
            out.push_back(dist_lo_stream[i]);
            out.push_back(dist_hi_stream[i]);
        }
    }

    return out;
}

} // namespace hakonyans::lossless_filter_lo_codec
