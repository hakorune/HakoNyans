#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossy_tile_packer {

inline std::vector<uint8_t> pack_band_group_tile(
    const std::vector<uint8_t>& dc_stream,
    const std::vector<uint8_t>& ac_low_stream,
    const std::vector<uint8_t>& ac_mid_stream,
    const std::vector<uint8_t>& ac_high_stream,
    const std::vector<uint8_t>& pindex_data,
    const std::vector<int8_t>& q_deltas,
    const std::vector<uint8_t>& cfl_data,
    const std::vector<uint8_t>& bt_data,
    const std::vector<uint8_t>& pal_data,
    const std::vector<uint8_t>& cpy_data
) {
    uint32_t sz[10] = {
        (uint32_t)dc_stream.size(),
        (uint32_t)ac_low_stream.size(),
        (uint32_t)ac_mid_stream.size(),
        (uint32_t)ac_high_stream.size(),
        (uint32_t)pindex_data.size(),
        (uint32_t)q_deltas.size(),
        (uint32_t)cfl_data.size(),
        (uint32_t)bt_data.size(),
        (uint32_t)pal_data.size(),
        (uint32_t)cpy_data.size()
    };

    std::vector<uint8_t> tile_data;
    tile_data.resize(40);
    std::memcpy(&tile_data[0], sz, 40);
    tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
    tile_data.insert(tile_data.end(), ac_low_stream.begin(), ac_low_stream.end());
    tile_data.insert(tile_data.end(), ac_mid_stream.begin(), ac_mid_stream.end());
    tile_data.insert(tile_data.end(), ac_high_stream.begin(), ac_high_stream.end());
    if (sz[4] > 0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
    if (sz[5] > 0) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data());
        tile_data.insert(tile_data.end(), p, p + sz[5]);
    }
    if (sz[6] > 0) tile_data.insert(tile_data.end(), cfl_data.begin(), cfl_data.end());
    if (sz[7] > 0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
    if (sz[8] > 0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
    if (sz[9] > 0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
    return tile_data;
}

inline std::vector<uint8_t> pack_legacy_tile(
    const std::vector<uint8_t>& dc_stream,
    const std::vector<uint8_t>& ac_stream,
    const std::vector<uint8_t>& pindex_data,
    const std::vector<int8_t>& q_deltas,
    const std::vector<uint8_t>& cfl_data,
    const std::vector<uint8_t>& bt_data,
    const std::vector<uint8_t>& pal_data,
    const std::vector<uint8_t>& cpy_data
) {
    uint32_t sz[8] = {
        (uint32_t)dc_stream.size(),
        (uint32_t)ac_stream.size(),
        (uint32_t)pindex_data.size(),
        (uint32_t)q_deltas.size(),
        (uint32_t)cfl_data.size(),
        (uint32_t)bt_data.size(),
        (uint32_t)pal_data.size(),
        (uint32_t)cpy_data.size()
    };

    std::vector<uint8_t> tile_data;
    tile_data.resize(32);
    std::memcpy(&tile_data[0], sz, 32);
    tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
    tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
    if (sz[2] > 0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
    if (sz[3] > 0) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data());
        tile_data.insert(tile_data.end(), p, p + sz[3]);
    }
    if (sz[4] > 0) tile_data.insert(tile_data.end(), cfl_data.begin(), cfl_data.end());
    if (sz[5] > 0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
    if (sz[6] > 0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
    if (sz[7] > 0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
    return tile_data;
}

} // namespace hakonyans::lossy_tile_packer
