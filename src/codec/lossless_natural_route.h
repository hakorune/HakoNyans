#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "../platform/thread_budget.h"
#include "zigzag.h"

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
#include <new>
#include <utility>
#include <vector>

namespace hakonyans::lossless_natural_route {


#include "lossless_natural_route_detail_impl.inc"

#include "lossless_natural_route_encode_padded_impl.inc"

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr,
    int mode2_nice_length_override = -1,
    int mode2_match_strategy_override = -1
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
        zigzag_encode_val,
        encode_byte_stream_shared_lz,
        encode_byte_stream,
        stats,
        mode2_nice_length_override,
        mode2_match_strategy_override
    );
}

} // namespace hakonyans::lossless_natural_route
