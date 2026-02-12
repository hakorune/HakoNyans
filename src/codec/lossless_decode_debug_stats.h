#pragma once

#include <cstdint>
#include <cstring>

namespace hakonyans {

struct LosslessDecodeDebugStats {
    // Top-level decode_color_lossless timings
    uint64_t decode_color_total_ns;
    uint64_t decode_header_dir_ns;
    uint64_t decode_plane_y_ns;
    uint64_t decode_plane_co_ns;
    uint64_t decode_plane_cg_ns;
    uint64_t decode_ycocg_to_rgb_ns;

    // decode_plane_lossless call envelope
    uint64_t decode_plane_total_ns;
    uint64_t decode_plane_calls;

    // Internal lossless plane decode stages
    uint64_t plane_try_natural_ns;
    uint64_t plane_screen_wrapper_ns;
    uint64_t plane_block_types_ns;
    uint64_t plane_filter_ids_ns;
    uint64_t plane_filter_lo_ns;
    uint64_t plane_filter_hi_ns;
    uint64_t plane_palette_ns;
    uint64_t plane_copy_ns;
    uint64_t plane_tile4_ns;
    uint64_t plane_residual_merge_ns;
    uint64_t plane_reconstruct_ns;
    uint64_t plane_crop_ns;

    LosslessDecodeDebugStats() { reset(); }
    void reset() { std::memset(this, 0, sizeof(*this)); }
};

} // namespace hakonyans
