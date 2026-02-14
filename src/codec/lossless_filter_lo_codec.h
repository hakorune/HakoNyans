#pragma once

#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_filter_lo_codec_utils.h"
#include "../platform/thread_budget.h"
#include "../platform/thread_pool.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <future>
#include <limits>
#include <chrono>
#include <vector>

namespace hakonyans::lossless_filter_lo_codec {

inline ThreadPool& lo_codec_worker_pool() {
    static ThreadPool pool((int)std::max(1u, thread_budget::max_threads(8)));
    return pool;
}

// profile_code: 0=UI, 1=ANIME, 2=PHOTO
// Returns encoded filter_lo payload (raw or wrapped).
template <typename EncodeByteStreamFn, typename EncodeByteStreamSharedLzFn, typename CompressLzFn>
inline std::vector<uint8_t> encode_filter_lo_stream(
    const std::vector<uint8_t>& lo_bytes,
    const std::vector<uint8_t>& filter_ids,
    const std::vector<FileHeader::BlockType>& block_types,
    uint32_t pad_h,
    int nx,
    int profile_code,
    LosslessModeDebugStats* stats,
    EncodeByteStreamFn&& encode_byte_stream,
    EncodeByteStreamSharedLzFn&& encode_byte_stream_shared_lz,
    CompressLzFn&& compress_lz,
    bool enable_lz_probe = false
) {
#include "lossless_filter_lo_codec_eval_base.inc"
#include "lossless_filter_lo_codec_eval_modes34.inc"
#include "lossless_filter_lo_codec_emit_output.inc"
}

} // namespace hakonyans::lossless_filter_lo_codec
