#pragma once

#include "headers.h"
#include "lz_tile.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace hakonyans::lossless_natural_route {

// Natural/photo-oriented route:
// Per-row predictor selection (SUB/UP/AVG), residual(zigzag16) serialization,
// then LZ + shared-CDF rANS on the residual byte stream.
template <typename ZigzagEncodeFn, typename EncodeSharedLzFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz
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
                int16_t pred = 0;
                if (p == 0) pred = left;                // SUB
                else if (p == 1) pred = up;            // UP
                else pred = (int16_t)(((int)left + (int)up) / 2); // AVG
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
            int16_t pred = 0;
            if (best_p == 0) pred = left;
            else if (best_p == 1) pred = up;
            else pred = (int16_t)(((int)left + (int)up) / 2);

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

} // namespace hakonyans::lossless_natural_route

