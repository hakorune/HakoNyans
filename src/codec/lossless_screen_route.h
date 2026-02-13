#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_screen_helpers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace hakonyans::lossless_screen_route {

using ScreenPreflightMetrics = lossless_screen::PreflightMetrics;

enum class ScreenBuildFailReason : uint8_t {
    NONE = 0,
    TOO_MANY_UNIQUE = 1,
    EMPTY_HIST = 2,
    INDEX_MISS = 3,
    INTERNAL = 4
};

inline ScreenPreflightMetrics analyze_screen_indexed_preflight(
    const int16_t* plane, uint32_t width, uint32_t height
) {
    return lossless_screen::analyze_preflight(plane, width, height);
}

// Screen-profile v1 candidate:
// [0xAD][mode:u8][bits:u8][reserved:u8][palette_count:u16][pixel_count:u32][raw_packed_size:u32]
// [palette:int16 * palette_count][payload]
// mode=0: raw packed index bytes, mode=1: rANS(payload), mode=2: LZ(payload)
template <typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_screen_indexed_tile_padded(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
    ScreenBuildFailReason* fail_reason,
    EncodeByteStreamFn&& encode_byte_stream
) {
    if (fail_reason) *fail_reason = ScreenBuildFailReason::NONE;
    if (!padded || pad_w == 0 || pad_h == 0) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::INTERNAL;
        return {};
    }
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::INTERNAL;
        return {};
    }

    thread_local std::array<uint32_t, 65536> seen_epoch{};
    thread_local std::array<uint8_t, 65536> value_index{};
    thread_local uint32_t epoch = 1;
    epoch++;
    if (epoch == 0) {
        seen_epoch.fill(0);
        epoch = 1;
    }

    std::vector<int16_t> unique_vals;
    std::vector<uint32_t> freqs;
    unique_vals.reserve(64);
    freqs.reserve(64);

    for (uint32_t i = 0; i < pixel_count; i++) {
        int16_t v = padded[i];
        uint16_t key = (uint16_t)v;
        if (seen_epoch[key] != epoch) {
            if (unique_vals.size() >= 64) {
                if (fail_reason) *fail_reason = ScreenBuildFailReason::TOO_MANY_UNIQUE;
                return {};
            }
            seen_epoch[key] = epoch;
            value_index[key] = (uint8_t)unique_vals.size();
            unique_vals.push_back(v);
            freqs.push_back(1);
        } else {
            freqs[value_index[key]]++;
        }
    }

    if (unique_vals.empty()) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::EMPTY_HIST;
        return {};
    }

    std::vector<uint8_t> order(unique_vals.size());
    std::iota(order.begin(), order.end(), (uint8_t)0);
    std::sort(order.begin(), order.end(), [&](uint8_t a, uint8_t b) {
        if (freqs[a] != freqs[b]) return freqs[a] > freqs[b];
        return unique_vals[a] < unique_vals[b];
    });

    std::vector<int16_t> palette_vals;
    palette_vals.reserve(order.size());
    for (size_t i = 0; i < order.size(); i++) {
        int16_t v = unique_vals[order[i]];
        palette_vals.push_back(v);
        value_index[(uint16_t)v] = (uint8_t)i;
    }

    const int bits_per_index = lossless_screen::bits_for_symbol_count((int)palette_vals.size());
    std::vector<uint8_t> indices(pixel_count, 0);
    for (uint32_t i = 0; i < pixel_count; i++) {
        int16_t v = padded[i];
        uint16_t key = (uint16_t)v;
        if (seen_epoch[key] != epoch) {
            if (fail_reason) *fail_reason = ScreenBuildFailReason::INDEX_MISS;
            return {};
        }
        indices[i] = value_index[key];
    }

    auto packed = lossless_screen::pack_index_bits(indices, bits_per_index);
    std::vector<uint8_t> payload = packed;
    uint8_t mode = 0;

    if (!packed.empty()) {
        auto packed_rans = encode_byte_stream(packed);
        if (!packed_rans.empty() && packed_rans.size() < payload.size()) {
            payload = std::move(packed_rans);
            mode = 1;
        }

        auto packed_lz = TileLZ::compress(packed);
        if (!packed_lz.empty() && packed_lz.size() < payload.size()) {
            payload = std::move(packed_lz);
            mode = 2;
        }
    }

    std::vector<uint8_t> out;
    out.reserve(14 + palette_vals.size() * 2 + payload.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_SCREEN_INDEXED);
    out.push_back(mode);
    out.push_back((uint8_t)bits_per_index);
    out.push_back(0);
    uint16_t pcount = (uint16_t)palette_vals.size();
    out.push_back((uint8_t)(pcount & 0xFF));
    out.push_back((uint8_t)((pcount >> 8) & 0xFF));
    out.push_back((uint8_t)(pixel_count & 0xFF));
    out.push_back((uint8_t)((pixel_count >> 8) & 0xFF));
    out.push_back((uint8_t)((pixel_count >> 16) & 0xFF));
    out.push_back((uint8_t)((pixel_count >> 24) & 0xFF));
    uint32_t raw_packed_size = (uint32_t)packed.size();
    out.push_back((uint8_t)(raw_packed_size & 0xFF));
    out.push_back((uint8_t)((raw_packed_size >> 8) & 0xFF));
    out.push_back((uint8_t)((raw_packed_size >> 16) & 0xFF));
    out.push_back((uint8_t)((raw_packed_size >> 24) & 0xFF));

    for (int16_t v : palette_vals) {
        uint16_t uv = (uint16_t)v;
        out.push_back((uint8_t)(uv & 0xFF));
        out.push_back((uint8_t)((uv >> 8) & 0xFF));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    if (fail_reason) *fail_reason = ScreenBuildFailReason::NONE;
    return out;
}

template <typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_screen_indexed_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ScreenBuildFailReason* fail_reason,
    EncodeByteStreamFn&& encode_byte_stream
) {
    if (!plane || width == 0 || height == 0) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::INTERNAL;
        return {};
    }
    const uint32_t pad_w = ((width + 7) / 8) * 8;
    const uint32_t pad_h = ((height + 7) / 8) * 8;
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::INTERNAL;
        return {};
    }
    std::vector<int16_t> padded(pixel_count, 0);
    for (uint32_t y = 0; y < pad_h; y++) {
        uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < pad_w; x++) {
            uint32_t sx = std::min(x, width - 1);
            padded[(size_t)y * pad_w + x] = plane[(size_t)sy * width + sx];
        }
    }
    return encode_plane_lossless_screen_indexed_tile_padded(
        padded.data(), pad_w, pad_h, fail_reason,
        std::forward<EncodeByteStreamFn>(encode_byte_stream)
    );
}

} // namespace hakonyans::lossless_screen_route
