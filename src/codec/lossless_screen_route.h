#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_screen_helpers.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
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
inline std::vector<uint8_t> encode_plane_lossless_screen_indexed_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ScreenBuildFailReason* fail_reason,
    EncodeByteStreamFn&& encode_byte_stream
) {
    if (fail_reason) *fail_reason = ScreenBuildFailReason::NONE;
    if (!plane || width == 0 || height == 0) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::INTERNAL;
        return {};
    }
    uint32_t pad_w = ((width + 7) / 8) * 8;
    uint32_t pad_h = ((height + 7) / 8) * 8;
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::INTERNAL;
        return {};
    }

    std::unordered_map<int16_t, uint32_t> freq;
    freq.reserve(128);
    for (uint32_t y = 0; y < pad_h; y++) {
        uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < pad_w; x++) {
            uint32_t sx = std::min(x, width - 1);
            int16_t v = plane[sy * width + sx];
            freq[v]++;
            if (freq.size() > 64) {
                if (fail_reason) *fail_reason = ScreenBuildFailReason::TOO_MANY_UNIQUE;
                return {};
            }
        }
    }
    if (freq.empty()) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::EMPTY_HIST;
        return {};
    }

    std::vector<std::pair<int16_t, uint32_t>> freq_pairs(freq.begin(), freq.end());
    std::sort(freq_pairs.begin(), freq_pairs.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (freq_pairs.size() > 64) {
        if (fail_reason) *fail_reason = ScreenBuildFailReason::TOO_MANY_UNIQUE;
        return {};
    }

    std::vector<int16_t> palette_vals;
    palette_vals.reserve(freq_pairs.size());
    std::unordered_map<int16_t, uint8_t> val_to_idx;
    val_to_idx.reserve(freq_pairs.size() * 2);
    for (size_t i = 0; i < freq_pairs.size(); i++) {
        palette_vals.push_back(freq_pairs[i].first);
        val_to_idx[freq_pairs[i].first] = (uint8_t)i;
    }

    const int bits_per_index = lossless_screen::bits_for_symbol_count((int)palette_vals.size());
    std::vector<uint8_t> indices;
    indices.reserve(pixel_count);
    for (uint32_t y = 0; y < pad_h; y++) {
        uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < pad_w; x++) {
            uint32_t sx = std::min(x, width - 1);
            int16_t v = plane[sy * width + sx];
            auto it = val_to_idx.find(v);
            if (it == val_to_idx.end()) {
                if (fail_reason) *fail_reason = ScreenBuildFailReason::INDEX_MISS;
                return {};
            }
            indices.push_back(it->second);
        }
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

} // namespace hakonyans::lossless_screen_route

