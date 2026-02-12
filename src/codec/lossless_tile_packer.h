#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_tile_packer {

inline std::vector<uint8_t> pack_tile_v2(
    const std::vector<uint8_t>& filter_ids_packed,
    const std::vector<uint8_t>& lo_stream,
    const std::vector<uint8_t>& hi_stream,
    uint32_t filter_pixel_count,
    const std::vector<uint8_t>& block_types,
    const std::vector<uint8_t>& palette_data,
    const std::vector<uint8_t>& copy_data,
    const std::vector<uint8_t>& tile4_data
) {
    uint32_t hdr[8] = {
        (uint32_t)filter_ids_packed.size(),
        (uint32_t)lo_stream.size(),
        (uint32_t)hi_stream.size(),
        filter_pixel_count,
        (uint32_t)block_types.size(),
        (uint32_t)palette_data.size(),
        (uint32_t)copy_data.size(),
        (uint32_t)tile4_data.size()
    };

    std::vector<uint8_t> tile_data;
    tile_data.resize(32);
    std::memcpy(tile_data.data(), hdr, 32);

    tile_data.insert(tile_data.end(), filter_ids_packed.begin(), filter_ids_packed.end());
    tile_data.insert(tile_data.end(), lo_stream.begin(), lo_stream.end());
    tile_data.insert(tile_data.end(), hi_stream.begin(), hi_stream.end());
    if (!block_types.empty()) tile_data.insert(tile_data.end(), block_types.begin(), block_types.end());
    if (!palette_data.empty()) tile_data.insert(tile_data.end(), palette_data.begin(), palette_data.end());
    if (!copy_data.empty()) tile_data.insert(tile_data.end(), copy_data.begin(), copy_data.end());
    if (!tile4_data.empty()) tile_data.insert(tile_data.end(), tile4_data.begin(), tile4_data.end());

    return tile_data;
}

} // namespace hakonyans::lossless_tile_packer
