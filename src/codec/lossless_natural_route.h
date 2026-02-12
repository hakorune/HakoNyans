#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_filter.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace hakonyans::lossless_natural_route {

namespace detail {

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

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> build_mode1_payload(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val, EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream
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

    auto resid_lz = TileLZ::compress(residual_bytes);
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

    // [magic][mode=1][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][resid_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + pred_payload.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(1);
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

    auto mode0 = detail::build_mode0_payload(
        padded.data(), pad_w, pad_h, pixel_count,
        zigzag_encode_val, encode_byte_stream_shared_lz
    );
    if (mode0.empty()) return {};

    auto mode1 = detail::build_mode1_payload(
        padded.data(), pad_w, pad_h, pixel_count,
        zigzag_encode_val, encode_byte_stream_shared_lz, encode_byte_stream
    );
    if (!mode1.empty() && mode1.size() * 1000ull <= mode0.size() * 995ull) {
        return mode1;
    }
    return mode0;
}

} // namespace hakonyans::lossless_natural_route
