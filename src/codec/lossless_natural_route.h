#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "../platform/thread_budget.h"

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
#include <utility>
#include <vector>

namespace hakonyans::lossless_natural_route {

namespace detail {

struct GlobalChainLzParams {
    int window_size = 65535;
    int chain_depth = 32;
    int min_dist_len3 = 128;
    int bias_permille = 990;
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
    uint64_t len3_reject_dist = 0;
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
        return t;
    }();
    return p;
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

    const size_t src_size = src.size();
    const uint8_t* s = src.data();
    if (counters) {
        counters->calls++;
        counters->src_bytes += (uint64_t)src_size;
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

    // `prev` is written for every position inserted into the current epoch chain.
    // We intentionally avoid full reinitialization and reuse capacity per thread.
    thread_local std::vector<int> prev;
    if (prev.size() < src_size) prev.resize(src_size);

    auto hash3 = [&](size_t p) -> uint32_t {
        uint32_t v = ((uint32_t)s[p] << 16) |
                     ((uint32_t)s[p + 1] << 8) |
                     (uint32_t)s[p + 2];
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

    size_t pos = 0;
    size_t lit_start = 0;
    while (pos + 2 < src_size) {
        const uint32_t h = hash3(pos);
        int ref = head_get(h);

        int best_len = 0;
        int best_dist = 0;
        int depth = 0;
        bool depth_limit_hit = false;
        bool early_maxlen_hit = false;
        while (ref >= 0 && depth < chain_depth) {
            if (counters) counters->chain_steps++;
            size_t ref_pos = (size_t)ref;
            int dist = (int)(pos - ref_pos);
            if (dist > 0 && dist <= window_size) {
                if (s[ref_pos] == s[pos] &&
                    s[ref_pos + 1] == s[pos + 1] &&
                    s[ref_pos + 2] == s[pos + 2]) {
                    int len = 3;
                    if (pos + 3 < src_size && s[ref_pos + 3] == s[pos + 3]) {
                        len = match_len_from(ref_pos, pos);
                    }
                    const bool acceptable = (len >= 4) || (len == 3 && dist <= min_dist_len3);
                    if (!acceptable && len == 3 && dist > min_dist_len3 && counters) {
                        counters->len3_reject_dist++;
                    }
                    if (acceptable && (len > best_len || (len == best_len && dist < best_dist))) {
                        best_len = len;
                        best_dist = dist;
                        if (best_len == 255) {
                            early_maxlen_hit = true;
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
        if (!early_maxlen_hit && ref >= 0 && depth >= chain_depth) {
            depth_limit_hit = true;
        }
        if (depth_limit_hit && counters) counters->depth_limit_hits++;
        if (early_maxlen_hit && counters) counters->early_maxlen_hits++;

        prev[pos] = head_get(h);
        head_set(h, (int)pos);

        if (best_len > 0) {
            flush_literals(lit_start, pos);
            const size_t out_pos = out.size();
            out.resize(out_pos + 4);
            uint8_t* dst = out.data() + out_pos;
            dst[0] = 1; // MATCH
            dst[1] = (uint8_t)best_len;
            dst[2] = (uint8_t)(best_dist & 0xFF);
            dst[3] = (uint8_t)((best_dist >> 8) & 0xFF);
            if (counters) {
                counters->match_count++;
                counters->match_bytes += (uint64_t)best_len;
            }

            for (int i = 1; i < best_len && pos + (size_t)i + 2 < src_size; i++) {
                size_t p = pos + (size_t)i;
                uint32_t h2 = hash3(p);
                prev[p] = head_get(h2);
                head_set(h2, (int)p);
            }

            pos += (size_t)best_len;
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
    prepared.residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = prepared.residual_bytes.data();

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        uint64_t cost3 = 0; // PAETH
        uint64_t cost4 = 0; // MED
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int16_t b = up_row ? up_row[x] : 0;
            const int16_t c = (up_row && x > 0) ? up_row[x - 1] : 0;
            const int pred2 = ((int)b / 2);
            const int pred3 = (int)LosslessFilter::paeth_predictor(0, b, c);
            const int pred4 = (int)LosslessFilter::med_predictor(0, b, c);
            cost0 += (uint64_t)std::abs(cur);
            cost1 += (uint64_t)std::abs(cur - (int)b);
            cost2 += (uint64_t)std::abs(cur - pred2);
            cost3 += (uint64_t)std::abs(cur - pred3);
            cost4 += (uint64_t)std::abs(cur - pred4);
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
            best_p = 4;
        }
        prepared.row_pred_ids[y] = (uint8_t)best_p;

        if (best_p == 0) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t a = (x > 0) ? row[x - 1] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)a);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else if (best_p == 1) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t b = up_row ? up_row[x] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)b);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else if (best_p == 2) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t a = (x > 0) ? row[x - 1] : 0;
                const int16_t b = up_row ? up_row[x] : 0;
                const int16_t pred = (int16_t)(((int)a + (int)b) / 2);
                const int16_t resid = (int16_t)((int)row[x] - (int)pred);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else if (best_p == 3) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t a = (x > 0) ? row[x - 1] : 0;
                const int16_t b = up_row ? up_row[x] : 0;
                const int16_t c = (up_row && x > 0) ? up_row[x - 1] : 0;
                const int16_t pred = LosslessFilter::paeth_predictor(a, b, c);
                const int16_t resid = (int16_t)((int)row[x] - (int)pred);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t a = (x > 0) ? row[x - 1] : 0;
                const int16_t b = up_row ? up_row[x] : 0;
                const int16_t c = (up_row && x > 0) ? up_row[x - 1] : 0;
                const int16_t pred = LosslessFilter::med_predictor(a, b, c);
                const int16_t resid = (int16_t)((int)row[x] - (int)pred);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
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

template <typename ZigzagEncodeFn,
          typename EncodeSharedLzFn,
          typename EncodeByteStreamFn,
          typename CompressResidualFn>
inline std::vector<uint8_t> build_mode1_payload(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val, EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    uint8_t out_mode,
    CompressResidualFn&& compress_residual
) {
    auto prepared = build_mode1_prepared(
        padded, pad_w, pad_h, pixel_count,
        std::forward<ZigzagEncodeFn>(zigzag_encode_val)
    );
    auto packed_pred = build_packed_predictor_stream(
        prepared.row_pred_ids,
        std::forward<EncodeByteStreamFn>(encode_byte_stream)
    );
    return build_mode1_payload_from_prepared(
        prepared, packed_pred, pad_h, pixel_count,
        std::forward<EncodeSharedLzFn>(encode_byte_stream_shared_lz),
        out_mode,
        std::forward<CompressResidualFn>(compress_residual)
    );
}

} // namespace detail

// Natural/photo-oriented route:
// mode0: row SUB/UP/AVG + residual LZ+rANS(shared CDF)
// mode1: row SUB/UP/AVG/PAETH/MED + compressed predictor stream
// mode2: mode1 predictor set + natural-only global-chain LZ for residual stream
template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile_padded(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr
) {
    using Clock = std::chrono::steady_clock;
    auto ns_since = [](const Clock::time_point& t0, const Clock::time_point& t1) -> uint64_t {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };
    if (!padded || pad_w == 0 || pad_h == 0) return {};
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) return {};

    const auto& lz_params = detail::global_chain_lz_runtime_params();
    std::vector<uint8_t> mode0;
    std::vector<uint8_t> mode1;
    std::vector<uint8_t> mode2;
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
        stats->natural_row_mode2_lz_len3_reject_dist += c.len3_reject_dist;
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
        if (stats) {
            stats->natural_row_prep_parallel_count++;
            stats->natural_row_prep_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
            stats->natural_row_mode12_parallel_count++;
            stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
        }

        std::promise<ReadyData> ready_promise;
        auto ready_future = ready_promise.get_future();
        auto mode2_future = std::async(
            std::launch::async,
            [&,
             rp = std::move(ready_promise)]() mutable -> TimedPayload {
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
                TimedPayload out;
                detail::GlobalChainLzCounters lz_counters;
                out.payload = detail::build_mode1_payload_from_prepared(
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
                out.lz = lz_counters;
                const auto t_mode2_1 = Clock::now();
                out.elapsed_ns = ns_since(t_mode2_0, t_mode2_1);
                return out;
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

        auto mode2_res = mode2_future.get();
        mode2 = std::move(mode2_res.payload);
        if (stats) stats->natural_row_mode2_build_ns += mode2_res.elapsed_ns;
        accumulate_mode2_lz(mode2_res.lz);
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
        if (mode12_tokens.acquired() && mode2_possible_vs_mode0) {
            if (stats) {
                stats->natural_row_mode12_parallel_count++;
                stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)mode12_tokens.count();
            }
            auto f_mode2 = std::async(std::launch::async, [&]() {
                thread_budget::ScopedParallelRegion guard;
                const auto t0 = Clock::now();
                TimedPayload out;
                detail::GlobalChainLzCounters lz_counters;
                out.payload = detail::build_mode1_payload_from_prepared(
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
                out.lz = lz_counters;
                const auto t1 = Clock::now();
                out.elapsed_ns = ns_since(t0, t1);
                return out;
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
            auto mode2_res = f_mode2.get();
            mode2 = std::move(mode2_res.payload);
            if (stats) stats->natural_row_mode2_build_ns += mode2_res.elapsed_ns;
            accumulate_mode2_lz(mode2_res.lz);
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
        }
    }
    if (mode0.empty()) return {};
    if (stats) {
        stats->natural_row_mode0_size_sum += (uint64_t)mode0.size();
        stats->natural_row_mode1_size_sum += (uint64_t)mode1.size();
        stats->natural_row_mode2_size_sum += (uint64_t)mode2.size();
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
    if (stats) {
        if (selected_mode == 0) stats->natural_row_mode0_selected_count++;
        else if (selected_mode == 1) stats->natural_row_mode1_selected_count++;
        else stats->natural_row_mode2_selected_count++;
    }
    return best;
}

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr
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
        zigzag_encode_val, encode_byte_stream_shared_lz, encode_byte_stream, stats
    );
}

} // namespace hakonyans::lossless_natural_route
