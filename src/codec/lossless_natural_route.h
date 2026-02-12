#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_filter.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace hakonyans::lossless_natural_route {

namespace detail {

struct GlobalChainLzParams {
    int window_size = 65535;
    int chain_depth = 32;
    int min_dist_len3 = 128;
    int bias_permille = 1000;
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
        t.bias_permille = parse_lz_env_int("HKN_LZ_BIAS_PERMILLE", 1000, 900, 1100);
        return t;
    }();
    return p;
}

inline std::vector<uint8_t> compress_global_chain_lz(
    const std::vector<uint8_t>& src, const GlobalChainLzParams& p
) {
    if (src.empty()) return {};

    const int HASH_BITS = 16;
    const int HASH_SIZE = 1 << HASH_BITS;
    const int window_size = p.window_size;
    const int chain_depth = p.chain_depth;
    const int min_dist_len3 = p.min_dist_len3;

    const size_t src_size = src.size();
    std::vector<uint8_t> out;
    out.reserve(src_size);

    std::vector<int> head(HASH_SIZE, -1);
    std::vector<int> prev(src_size, -1);

    auto hash3 = [&](size_t p) -> uint32_t {
        uint32_t v = ((uint32_t)src[p] << 16) |
                     ((uint32_t)src[p + 1] << 8) |
                     (uint32_t)src[p + 2];
        return (v * 0x1e35a7bdu) >> (32 - HASH_BITS);
    };

    auto flush_literals = [&](size_t start, size_t end) {
        size_t cur = start;
        while (cur < end) {
            size_t chunk = std::min<size_t>(255, end - cur);
            out.push_back(0); // LITRUN
            out.push_back((uint8_t)chunk);
            out.insert(out.end(), src.data() + cur, src.data() + cur + chunk);
            cur += chunk;
        }
    };

    size_t pos = 0;
    size_t lit_start = 0;
    while (pos + 2 < src_size) {
        const uint32_t h = hash3(pos);
        int ref = head[h];

        int best_len = 0;
        int best_dist = 0;
        int depth = 0;
        while (ref >= 0 && depth < chain_depth) {
            size_t ref_pos = (size_t)ref;
            int dist = (int)(pos - ref_pos);
            if (dist > 0 && dist <= window_size) {
                if (src[ref_pos] == src[pos] &&
                    src[ref_pos + 1] == src[pos + 1] &&
                    src[ref_pos + 2] == src[pos + 2]) {
                    int len = 3;
                    while (pos + (size_t)len < src_size &&
                           ref_pos + (size_t)len < src_size &&
                           len < 255 &&
                           src[ref_pos + (size_t)len] == src[pos + (size_t)len]) {
                        len++;
                    }
                    const bool acceptable = (len >= 4) || (len == 3 && dist <= min_dist_len3);
                    if (acceptable && (len > best_len || (len == best_len && dist < best_dist))) {
                        best_len = len;
                        best_dist = dist;
                        if (best_len == 255) break;
                    }
                }
            } else if (dist > window_size) {
                break;
            }
            ref = prev[ref_pos];
            depth++;
        }

        prev[pos] = head[h];
        head[h] = (int)pos;

        if (best_len > 0) {
            flush_literals(lit_start, pos);
            out.push_back(1); // MATCH
            out.push_back((uint8_t)best_len);
            out.push_back((uint8_t)(best_dist & 0xFF));
            out.push_back((uint8_t)((best_dist >> 8) & 0xFF));

            for (int i = 1; i < best_len && pos + (size_t)i + 2 < src_size; i++) {
                size_t p = pos + (size_t)i;
                uint32_t h2 = hash3(p);
                prev[p] = head[h2];
                head[h2] = (int)p;
            }

            pos += (size_t)best_len;
            lit_start = pos;
        } else {
            pos++;
        }
    }

    flush_literals(lit_start, src_size);
    return out;
}

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn>
inline std::vector<uint8_t> build_mode0_payload(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val, EncodeSharedLzFn&& encode_byte_stream_shared_lz
) {
    std::vector<uint8_t> row_pred_ids(pad_h, 0);
    std::vector<uint8_t> residual_bytes;
    residual_bytes.reserve((size_t)pixel_count * 2);

    std::vector<int16_t> recon(pixel_count, 0);
    for (uint32_t y = 0; y < pad_h; y++) {
        int best_p = 0;
        uint64_t best_cost = std::numeric_limits<uint64_t>::max();

        for (int p = 0; p < 3; p++) {
            uint64_t cost = 0;
            for (uint32_t x = 0; x < pad_w; x++) {
                int16_t left = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
                int16_t up = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
                int16_t pred = (p == 0) ? left : (p == 1 ? up : (int16_t)(((int)left + (int)up) / 2));
                int16_t cur = padded[(size_t)y * pad_w + x];
                cost += (uint64_t)std::abs((int)cur - (int)pred);
            }
            if (cost < best_cost) {
                best_cost = cost;
                best_p = p;
            }
        }
        row_pred_ids[y] = (uint8_t)best_p;

        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t left = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t up = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t pred = (best_p == 0) ? left : (best_p == 1 ? up : (int16_t)(((int)left + (int)up) / 2));
            int16_t cur = padded[(size_t)y * pad_w + x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);

            uint16_t zz = zigzag_encode_val(resid);
            residual_bytes.push_back((uint8_t)(zz & 0xFF));
            residual_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
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
    std::vector<uint8_t> row_pred_ids(pad_h, 0);
    std::vector<uint8_t> residual_bytes;
    residual_bytes.reserve((size_t)pixel_count * 2);

    std::vector<int16_t> recon(pixel_count, 0);
    for (uint32_t y = 0; y < pad_h; y++) {
        int best_p = 0;
        uint64_t best_cost = std::numeric_limits<uint64_t>::max();

        for (int p = 0; p < 5; p++) {
            uint64_t cost = 0;
            for (uint32_t x = 0; x < pad_w; x++) {
                int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
                int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
                int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
                int16_t pred = 0;
                if (p == 0) pred = a; // SUB
                else if (p == 1) pred = b; // UP
                else if (p == 2) pred = (int16_t)(((int)a + (int)b) / 2); // AVG
                else if (p == 3) pred = LosslessFilter::paeth_predictor(a, b, c); // PAETH
                else pred = LosslessFilter::med_predictor(a, b, c); // MED
                int16_t cur = padded[(size_t)y * pad_w + x];
                cost += (uint64_t)std::abs((int)cur - (int)pred);
            }
            if (cost < best_cost) {
                best_cost = cost;
                best_p = p;
            }
        }
        row_pred_ids[y] = (uint8_t)best_p;

        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (best_p == 0) pred = a;
            else if (best_p == 1) pred = b;
            else if (best_p == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (best_p == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else pred = LosslessFilter::med_predictor(a, b, c);

            int16_t cur = padded[(size_t)y * pad_w + x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);

            uint16_t zz = zigzag_encode_val(resid);
            residual_bytes.push_back((uint8_t)(zz & 0xFF));
            residual_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
        }
    }

    auto resid_lz = compress_residual(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    std::vector<uint8_t> pred_payload = row_pred_ids;
    uint8_t pred_mode = 0; // raw
    auto pred_rans = encode_byte_stream(row_pred_ids);
    if (!pred_rans.empty() && pred_rans.size() < pred_payload.size()) {
        pred_payload = std::move(pred_rans);
        pred_mode = 1; // rANS
    }

    // [magic][mode=1/2][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][resid_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + pred_payload.size() + resid_lz_rans.size());
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
    out.push_back(pred_mode);
    push_u32(pred_count);
    push_u32((uint32_t)pred_payload.size());
    out.insert(out.end(), pred_payload.begin(), pred_payload.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

} // namespace detail

// Natural/photo-oriented route:
// mode0: row SUB/UP/AVG + residual LZ+rANS(shared CDF)
// mode1: row SUB/UP/AVG/PAETH/MED + compressed predictor stream
// mode2: mode1 predictor set + natural-only global-chain LZ for residual stream
template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream
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

    const auto& lz_params = detail::global_chain_lz_runtime_params();

    auto mode0 = detail::build_mode0_payload(
        padded.data(), pad_w, pad_h, pixel_count,
        zigzag_encode_val, encode_byte_stream_shared_lz
    );
    if (mode0.empty()) return {};

    auto mode1 = detail::build_mode1_payload(
        padded.data(), pad_w, pad_h, pixel_count,
        zigzag_encode_val, encode_byte_stream_shared_lz, encode_byte_stream,
        1,
        [](const std::vector<uint8_t>& bytes) {
            return TileLZ::compress(bytes);
        }
    );

    auto mode2 = detail::build_mode1_payload(
        padded.data(), pad_w, pad_h, pixel_count,
        zigzag_encode_val, encode_byte_stream_shared_lz, encode_byte_stream,
        2,
        [&](const std::vector<uint8_t>& bytes) {
            return detail::compress_global_chain_lz(bytes, lz_params);
        }
    );

    std::vector<uint8_t> best = std::move(mode0);
    if (!mode1.empty() && mode1.size() < best.size()) {
        best = std::move(mode1);
    }
    if (!mode2.empty()) {
        const uint64_t lhs = (uint64_t)mode2.size() * 1000ull;
        const uint64_t rhs = (uint64_t)best.size() * (uint64_t)lz_params.bias_permille;
        if (lhs <= rhs) {
            best = std::move(mode2);
        }
    }
    return best;
}

} // namespace hakonyans::lossless_natural_route
