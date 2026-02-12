#pragma once

#include "headers.h"
#include "lz_tile.h"
#include <cstdint>
#include <vector>

namespace hakonyans::lossless_tile4_codec {

/**
 * Result of Tile4 (4x4 quadrant match) encoding.
 * Contains 4 indices into the 16-element candidate array.
 */
struct Tile4Result {
    uint8_t indices[4];
};

/**
 * Convert Tile4 results to raw byte stream.
 * Each Tile4Result produces 2 bytes: [indices1|indices0][indices3|indices2].
 * Each index is 4 bits (0-15).
 */
inline std::vector<uint8_t> serialize_tile4_raw(const std::vector<Tile4Result>& tile4_results) {
    std::vector<uint8_t> tile4_raw;
    for (const auto& res : tile4_results) {
        tile4_raw.push_back((uint8_t)((res.indices[1] << 4) | (res.indices[0] & 0x0F)));
        tile4_raw.push_back((uint8_t)((res.indices[3] << 4) | (res.indices[2] & 0x0F)));
    }
    return tile4_raw;
}

/**
 * Encode Tile4 stream with wrapper selection.
 * Tries rANS and LZ compression, selects smallest.
 *
 * Template parameters:
 * - EncodeByteStreamFn: callable to encode byte stream with rANS
 *
 * Format: [magic][mode][raw_count:u32][payload]
 * - mode=0: raw (no wrapper)
 * - mode=1: rANS wrapper
 * - mode=2: LZ wrapper
 */
template <typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_tile4_stream(
    const std::vector<Tile4Result>& tile4_results,
    EncodeByteStreamFn&& encode_byte_stream
) {
    std::vector<uint8_t> tile4_raw = serialize_tile4_raw(tile4_results);
    if (tile4_raw.empty()) return tile4_raw;

    std::vector<uint8_t> tile4_data = tile4_raw;
    int tile4_mode = 0; // 0=raw, 1=rANS, 2=LZ

    // Try rANS wrapper
    auto tile4_rans = encode_byte_stream(tile4_raw);
    if (!tile4_rans.empty()) {
        std::vector<uint8_t> wrapped;
        wrapped.reserve(1 + 1 + 4 + tile4_rans.size());
        wrapped.push_back(FileHeader::WRAPPER_MAGIC_TILE4);
        wrapped.push_back(1); // mode = rANS
        uint32_t raw_count = (uint32_t)tile4_raw.size();
        wrapped.resize(wrapped.size() + 4);
        std::memcpy(wrapped.data() + 2, &raw_count, 4);
        wrapped.insert(wrapped.end(), tile4_rans.begin(), tile4_rans.end());
        if (wrapped.size() < tile4_data.size()) {
            tile4_data = std::move(wrapped);
            tile4_mode = 1;
        }
    }

    // Try LZ wrapper
    auto tile4_lz = TileLZ::compress(tile4_raw);
    if (!tile4_lz.empty()) {
        std::vector<uint8_t> wrapped;
        wrapped.reserve(1 + 1 + 4 + tile4_lz.size());
        wrapped.push_back(FileHeader::WRAPPER_MAGIC_TILE4);
        wrapped.push_back(2); // mode = LZ
        uint32_t raw_count = (uint32_t)tile4_raw.size();
        wrapped.resize(wrapped.size() + 4);
        std::memcpy(wrapped.data() + 2, &raw_count, 4);
        wrapped.insert(wrapped.end(), tile4_lz.begin(), tile4_lz.end());
        if (wrapped.size() < tile4_data.size()) {
            tile4_data = std::move(wrapped);
            tile4_mode = 2;
        }
    }

    return tile4_data;
}

} // namespace hakonyans::lossless_tile4_codec
