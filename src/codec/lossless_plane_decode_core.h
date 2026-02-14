#pragma once

#include "copy.h"
#include "headers.h"
#include "lossless_block_types_codec.h"
#include "lossless_filter.h"
#include "lossless_filter_lo_decode.h"
#include "lossless_natural_decode.h"
#include "lossless_decode_debug_stats.h"
#include "lossless_tile4_codec.h"
#include "lz_tile.h"
#include "palette.h"
#include "zigzag.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_plane_decode_core {

template <typename DecodeByteStreamFn, typename DecodeSharedLzFn>
inline std::vector<int16_t> decode_plane_lossless(
    const uint8_t* td,
    size_t ts,
    uint32_t width,
    uint32_t height,
    uint16_t file_version,
    DecodeByteStreamFn&& decode_byte_stream,
    DecodeSharedLzFn&& decode_byte_stream_shared_lz,
    ::hakonyans::LosslessDecodeDebugStats* perf_stats = nullptr
) {
#include "lossless_plane_decode_core_stage1.inc"
#include "lossless_plane_decode_core_stage2.inc"
#include "lossless_plane_decode_core_stage3.inc"
}

} // namespace hakonyans::lossless_plane_decode_core
