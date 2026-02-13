#pragma once

#include "headers.h"
#include "lossless_filter.h"
#include "lz_tile.h"
#include "zigzag.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_natural_decode {

template <typename DecodeByteStreamSharedLzFn, typename DecodeByteStreamFn>
inline bool try_decode_natural_row_wrapper(
    const uint8_t* td,
    size_t ts,
    uint32_t width,
    uint32_t height,
    uint32_t pad_w,
    uint32_t pad_h,
    uint16_t file_version,
    DecodeByteStreamSharedLzFn&& decode_byte_stream_shared_lz,
    DecodeByteStreamFn&& decode_byte_stream,
    std::vector<int16_t>& out
) {
    if (!(ts >= 18 &&
          file_version >= FileHeader::VERSION_NATURAL_ROW_ROUTE &&
          td[0] == FileHeader::WRAPPER_MAGIC_NATURAL_ROW)) {
        return false;
    }

    auto read_u32 = [](const uint8_t* p) -> uint32_t {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    };

    const std::vector<int16_t> zeros(width * height, 0);

    uint8_t mode = td[1];
    if (mode != 0 && mode != 1 && mode != 2 && mode != 3) {
        out = zeros;
        return true;
    }
    if (mode == 2 && file_version < FileHeader::VERSION_NATURAL_GLOBAL_CHAIN_ROUTE) {
        out = zeros;
        return true;
    }
    if (mode == 3 && file_version < FileHeader::VERSION_NATURAL_CONTEXT_ROUTE) {
        out = zeros;
        return true;
    }

    uint32_t pixel_count = read_u32(td + 2);
    uint32_t pred_count = read_u32(td + 6);
    
    // mode 0/1/2 use these slots for resid_raw_count/resid_payload_size.
    // mode 3 uses them for flat_payload_size/edge_payload_size.
    uint32_t val3 = read_u32(td + 10);
    uint32_t val4 = read_u32(td + 14);

    uint32_t expected_pixels = pad_w * pad_h;
    if (pixel_count != expected_pixels || pred_count != pad_h) {
        out = zeros;
        return true;
    }

    std::vector<uint8_t> pred_ids(pred_count, 0);
    const uint8_t* flat_ptr = nullptr;
    const uint8_t* edge_ptr = nullptr;
    uint32_t flat_size = 0;
    uint32_t edge_size = 0;
    const uint8_t* lz_resid_ptr = nullptr;
    uint32_t lz_resid_size = 0;
    uint32_t resid_raw_count = 0;

    if (mode == 0) {
        resid_raw_count = val3;
        lz_resid_size = val4;
        if (resid_raw_count != expected_pixels * 2) {
            out = zeros; return true;
        }
        size_t pred_off = 18;
        size_t resid_off = pred_off + pred_count;
        if (resid_off > ts || lz_resid_size > ts - resid_off) {
            out = zeros; return true;
        }
        std::memcpy(pred_ids.data(), td + pred_off, pred_count);
        lz_resid_ptr = td + resid_off;
    } else {
        if (ts < 27) {
            out = zeros; return true;
        }
        uint8_t pred_mode = td[18];
        uint32_t pred_raw_count = read_u32(td + 19);
        uint32_t pred_payload_size = read_u32(td + 23);
        if (pred_raw_count != pred_count) {
            out = zeros; return true;
        }
        size_t pred_payload_off = 27;
        if (pred_payload_off > ts || pred_payload_size > ts - pred_payload_off) {
            out = zeros; return true;
        }
        const uint8_t* pred_payload_ptr = td + pred_payload_off;
        if (pred_mode == 0) {
            if (pred_payload_size < pred_count) {
                out = zeros; return true;
            }
            std::memcpy(pred_ids.data(), pred_payload_ptr, pred_count);
        } else if (pred_mode == 1) {
            pred_ids = decode_byte_stream(pred_payload_ptr, pred_payload_size, pred_count);
            if (pred_ids.size() != pred_count) {
                out = zeros; return true;
            }
        } else {
            out = zeros; return true;
        }

        size_t resid_off = pred_payload_off + pred_payload_size;
        if (mode == 3) {
            flat_size = val3;
            edge_size = val4;
            if (resid_off > ts || flat_size > ts - resid_off || edge_size > ts - resid_off - flat_size) {
                out = zeros; return true;
            }
            flat_ptr = td + resid_off;
            edge_ptr = flat_ptr + flat_size;
        } else {
            resid_raw_count = val3;
            lz_resid_size = val4;
            if (resid_raw_count != expected_pixels * 2) {
                out = zeros; return true;
            }
            if (resid_off > ts || lz_resid_size > ts - resid_off) {
                out = zeros; return true;
            }
            lz_resid_ptr = td + resid_off;
        }
    }

    std::vector<uint8_t> flat_bytes;
    std::vector<uint8_t> edge_bytes;
    std::vector<uint8_t> resid_lz_bytes;

    if (mode == 3) {
        if (flat_size > 0) {
            flat_bytes = decode_byte_stream(flat_ptr, flat_size, 0);
        }
        if (edge_size > 0) {
            edge_bytes = decode_byte_stream(edge_ptr, edge_size, 0);
        }
    } else {
        auto lz_payload = decode_byte_stream_shared_lz(lz_resid_ptr, lz_resid_size, 0);
        if (lz_payload.empty()) {
            out = zeros; return true;
        }
        resid_lz_bytes = TileLZ::decompress(lz_payload.data(), lz_payload.size(), resid_raw_count);
        if (resid_lz_bytes.size() != resid_raw_count) {
            out = zeros; return true;
        }
    }

    std::vector<int16_t> padded(expected_pixels, 0);
    size_t rb = 0;
    size_t fb = 0;
    size_t eb = 0;

    for (uint32_t y = 0; y < pad_h; y++) {
        uint8_t pid = pred_ids[y];
        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? padded[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? padded[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? padded[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;

            if (mode == 0) {
                if (pid == 0) pred = a;
                else if (pid == 1) pred = b;
                else pred = (int16_t)(((int)a + (int)b) / 2);
            } else {
                if (pid == 0) pred = a;
                else if (pid == 1) pred = b;
                else if (pid == 2) pred = (int16_t)(((int)a + (int)b) / 2);
                else if (pid == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
                else if (pid == 4) pred = LosslessFilter::med_predictor(a, b, c);
                else if (pid == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
                else if (pid == 6) pred = (int16_t)(((int)a + (int)b * 3) / 4);
                else pred = 0;
            }

            uint16_t zz = 0;
            if (mode == 3) {
                int grad = std::max(std::abs(a - c), std::abs(b - c));
                if (grad < 16) {
                    if (fb + 1 >= flat_bytes.size()) { out = zeros; return true; }
                    zz = (uint16_t)flat_bytes[fb] | ((uint16_t)flat_bytes[fb + 1] << 8);
                    fb += 2;
                } else {
                    if (eb + 1 >= edge_bytes.size()) { out = zeros; return true; }
                    zz = (uint16_t)edge_bytes[eb] | ((uint16_t)edge_bytes[eb + 1] << 8);
                    eb += 2;
                }
            } else {
                if (rb + 1 >= resid_lz_bytes.size()) { out = zeros; return true; }
                zz = (uint16_t)resid_lz_bytes[rb] | ((uint16_t)resid_lz_bytes[rb + 1] << 8);
                rb += 2;
            }

            int16_t resid = zigzag_decode_val(zz);
            padded[(size_t)y * pad_w + x] = (int16_t)(pred + resid);
        }
    }

    std::vector<int16_t> result(width * height, 0);
    for (uint32_t y = 0; y < height; y++) {
        std::memcpy(&result[y * width], &padded[y * pad_w], width * sizeof(int16_t));
    }

    out = std::move(result);
    return true;
}

} // namespace hakonyans::lossless_natural_decode
