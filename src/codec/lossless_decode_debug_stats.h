#pragma once

#include <cstddef>
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
    uint64_t decode_plane_dispatch_ns;
    uint64_t decode_plane_wait_ns;
    uint64_t decode_ycocg_dispatch_ns;
    uint64_t decode_ycocg_kernel_ns;
    uint64_t decode_ycocg_wait_ns;
    uint64_t decode_ycocg_rows_sum;
    uint64_t decode_ycocg_pixels_sum;

    // Decode parallel scheduler telemetry.
    uint64_t decode_plane_parallel_3way_count;
    uint64_t decode_plane_parallel_seq_count;
    uint64_t decode_plane_parallel_tokens_sum;
    uint64_t decode_ycocg_parallel_count;
    uint64_t decode_ycocg_sequential_count;
    uint64_t decode_ycocg_parallel_threads_sum;

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

    // filter_lo decode internals
    uint64_t plane_filter_lo_mode_raw_count;
    uint64_t plane_filter_lo_mode1_count;
    uint64_t plane_filter_lo_mode2_count;
    uint64_t plane_filter_lo_mode3_count;
    uint64_t plane_filter_lo_mode4_count;
    uint64_t plane_filter_lo_mode5_count;
    uint64_t plane_filter_lo_mode6_count;
    uint64_t plane_filter_lo_mode_invalid_count;
    uint64_t plane_filter_lo_mode5_shared_cdf_count;
    uint64_t plane_filter_lo_mode5_legacy_cdf_count;
    uint64_t plane_filter_lo_mode6_shared_cdf_count;
    uint64_t plane_filter_lo_mode6_legacy_cdf_count;
    uint64_t plane_filter_lo_fallback_zero_fill_count;
    uint64_t plane_filter_lo_zero_pad_bytes_sum;
    uint64_t plane_filter_lo_mode3_active_rows_sum;
    uint64_t plane_filter_lo_mode4_nonempty_ctx_sum;
    uint64_t plane_filter_lo_mode4_parallel_ctx_tiles;
    uint64_t plane_filter_lo_mode4_sequential_ctx_tiles;
    uint64_t plane_filter_lo_decode_rans_ns;
    uint64_t plane_filter_lo_decode_shared_rans_ns;
    uint64_t plane_filter_lo_tilelz_decompress_ns;
    uint64_t plane_filter_lo_mode3_row_lens_ns;
    uint64_t plane_filter_lo_mode4_row_lens_ns;

    // plane_reconstruct internals
    uint64_t plane_recon_block_palette_count;
    uint64_t plane_recon_block_copy_count;
    uint64_t plane_recon_block_tile4_count;
    uint64_t plane_recon_block_dct_count;
    uint64_t plane_recon_copy_fast_rows;
    uint64_t plane_recon_copy_slow_rows;
    uint64_t plane_recon_copy_clamped_pixels;
    uint64_t plane_recon_tile4_fast_quads;
    uint64_t plane_recon_tile4_slow_quads;
    uint64_t plane_recon_tile4_clamped_pixels;
    uint64_t plane_recon_dct_pixels;
    uint64_t plane_recon_residual_consumed;
    uint64_t plane_recon_residual_missing;

    LosslessDecodeDebugStats() { reset(); }
    void reset() { std::memset(this, 0, sizeof(*this)); }
    void accumulate_from(const LosslessDecodeDebugStats& other) {
        static_assert(sizeof(LosslessDecodeDebugStats) % sizeof(uint64_t) == 0,
                      "LosslessDecodeDebugStats must be uint64_t aligned");
        auto* dst = reinterpret_cast<uint64_t*>(this);
        const auto* src = reinterpret_cast<const uint64_t*>(&other);
        constexpr size_t kWords = sizeof(LosslessDecodeDebugStats) / sizeof(uint64_t);
        for (size_t i = 0; i < kWords; i++) dst[i] += src[i];
    }
};

} // namespace hakonyans
