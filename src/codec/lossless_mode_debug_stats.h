#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hakonyans {

struct LosslessModeDebugStats {
    uint64_t total_blocks;
    uint64_t tile4_candidates;
    uint64_t copy_candidates;
    uint64_t palette_candidates;
    uint64_t copy_palette_overlap;

    uint64_t tile4_selected;
    uint64_t copy_selected;
    uint64_t palette_selected;
    uint64_t filter_selected;
    uint64_t filter_med_selected;
    uint64_t palette_rescue_attempted;
    uint64_t palette_rescue_adopted;
    uint64_t palette_rescue_gain_bits_sum;

    uint64_t filter_blocks_with_copy_candidate;
    uint64_t filter_blocks_with_palette_candidate;
    uint64_t filter_blocks_unique_le2;
    uint64_t filter_blocks_unique_le4;
    uint64_t filter_blocks_unique_le8;
    uint64_t filter_blocks_unique_gt8;
    uint64_t filter_blocks_transitions_sum;
    uint64_t filter_blocks_variance_proxy_sum;
    uint64_t filter_blocks_est_filter_bits_sum;
    uint64_t filter_diag_palette16_candidates;
    uint64_t filter_diag_palette16_better;
    uint64_t filter_diag_palette16_size_sum;
    uint64_t filter_diag_palette16_est_bits_sum;
    uint64_t filter_diag_palette16_gain_bits_sum;
    uint64_t filter_rows_with_pixels;
    uint64_t filter_row_id_hist[6];

    uint64_t tile4_rejected_by_copy;
    uint64_t tile4_rejected_by_palette;
    uint64_t tile4_rejected_by_filter;
    uint64_t copy_rejected_by_tile4;
    uint64_t copy_rejected_by_palette;
    uint64_t copy_rejected_by_filter;
    uint64_t palette_rejected_by_tile4;
    uint64_t palette_rejected_by_copy;
    uint64_t palette_rejected_by_filter;

    uint64_t est_copy_bits_sum;
    uint64_t est_tile4_bits_sum;
    uint64_t est_palette_bits_sum;
    uint64_t est_filter_bits_sum;
    uint64_t est_selected_bits_sum;
    uint64_t est_copy_loss_bits_sum;
    uint64_t est_tile4_loss_bits_sum;
    uint64_t est_palette_loss_bits_sum;

    uint64_t block_types_bytes_sum;
    uint64_t block_type_runs_sum;
    uint64_t block_type_short_runs;
    uint64_t block_type_long_runs;
    uint64_t block_type_runs_dct;
    uint64_t block_type_runs_palette;
    uint64_t block_type_runs_copy;
    uint64_t block_type_runs_tile4;
    uint64_t block_types_lz_used_count;
    uint64_t block_types_lz_saved_bytes_sum;

    uint64_t palette_lz_used_count;
    uint64_t palette_lz_saved_bytes_sum;
    uint64_t palette_stream_bytes_sum;
    uint64_t palette_stream_raw_bytes_sum;
    uint64_t palette_stream_v2_count;
    uint64_t palette_stream_v3_count;
    uint64_t palette_stream_mask_dict_count;
    uint64_t palette_stream_mask_dict_entries;
    uint64_t palette_stream_palette_dict_count;
    uint64_t palette_stream_palette_dict_entries;
    uint64_t palette_blocks_parsed;
    uint64_t palette_blocks_prev_reuse;
    uint64_t palette_blocks_dict_ref;
    uint64_t palette_blocks_raw_colors;
    uint64_t palette_blocks_two_color;
    uint64_t palette_blocks_multi_color;
    uint64_t palette_parse_errors;
    uint64_t palette_stream_compact_count;
    uint64_t palette_stream_compact_saved_bytes_sum;

    uint64_t copy_stream_count;
    uint64_t copy_stream_mode0;
    uint64_t copy_stream_mode1;
    uint64_t copy_stream_mode2;
    uint64_t copy_stream_mode3;
    uint64_t copy_wrap_mode0;
    uint64_t copy_wrap_mode1;
    uint64_t copy_wrap_mode2;
    uint64_t copy_mode3_run_tokens_sum;
    uint64_t copy_mode3_runs_sum;
    uint64_t copy_mode3_long_runs;
    uint64_t copy_lz_used_count;
    uint64_t copy_lz_saved_bytes_sum;
    uint64_t copy_stream_bytes_sum;
    uint64_t copy_stream_payload_bits_sum;
    uint64_t copy_stream_overhead_bits_sum;
    uint64_t copy_ops_total;
    uint64_t copy_ops_small;
    uint64_t copy_ops_raw;
    uint64_t copy_mode2_zero_bit_streams;
    uint64_t copy_mode2_dynamic_bits_sum;
    uint64_t tile4_stream_mode0;
    uint64_t tile4_stream_mode1;
    uint64_t tile4_stream_mode2;
    uint64_t tile4_stream_raw_bytes_sum;
    uint64_t tile4_stream_bytes_sum;

    uint64_t filter_ids_mode0;
    uint64_t filter_ids_mode1;
    uint64_t filter_ids_mode2;
    uint64_t filter_ids_raw_bytes_sum;
    uint64_t filter_ids_compressed_bytes_sum;
    uint64_t filter_hi_sparse_count;
    uint64_t filter_hi_dense_count;
    uint64_t filter_hi_zero_ratio_sum;
    uint64_t filter_hi_raw_bytes_sum;
    uint64_t filter_hi_compressed_bytes_sum;

    uint64_t filter_lo_mode0;
    uint64_t filter_lo_mode1;
    uint64_t filter_lo_mode2;
    uint64_t filter_lo_raw_bytes_sum;
    uint64_t filter_lo_compressed_bytes_sum;

    uint64_t filter_lo_mode3;
    uint64_t filter_lo_mode3_rows_sum;
    uint64_t filter_lo_mode3_saved_bytes_sum;
    uint64_t filter_lo_mode3_pred_hist[4];
    uint64_t filter_lo_mode4;
    uint64_t filter_lo_mode4_saved_bytes_sum;
    uint64_t filter_lo_mode5;
    uint64_t filter_lo_mode5_saved_bytes_sum;
    uint64_t filter_lo_ctx_nonempty_tiles;
    uint64_t filter_lo_ctx_bytes_sum[6];

    uint64_t filter_lo_mode2_reject_gate;
    uint64_t filter_lo_mode4_reject_gate;
    uint64_t filter_lo_mode5_candidates;
    uint64_t filter_lo_mode5_reject_gate;
    uint64_t filter_lo_mode5_reject_best;
    uint64_t filter_lo_mode2_candidate_bytes_sum;
    uint64_t filter_lo_mode4_candidate_bytes_sum;
    uint64_t filter_lo_mode5_candidate_bytes_sum;
    uint64_t filter_lo_mode5_wrapped_bytes_sum;
    uint64_t filter_lo_mode5_legacy_bytes_sum;

    uint64_t screen_candidate_count;
    uint64_t screen_selected_count;
    uint64_t screen_rejected_pre_gate;
    uint64_t screen_rejected_cost_gate;
    uint64_t screen_rejected_small_tile;
    uint64_t screen_rejected_prefilter_texture;
    uint64_t screen_rejected_build_fail;
    uint64_t screen_build_fail_too_many_unique;
    uint64_t screen_build_fail_empty_hist;
    uint64_t screen_build_fail_index_miss;
    uint64_t screen_build_fail_other;
    uint64_t screen_rejected_palette_limit;
    uint64_t screen_rejected_bits_limit;
    uint64_t screen_mode0_reject_count;
    uint64_t screen_ui_like_count;
    uint64_t screen_anime_like_count;
    uint64_t screen_palette_count_sum;
    uint64_t screen_bits_per_index_sum;
    uint64_t screen_gain_bytes_sum;
    uint64_t screen_loss_bytes_sum;
    uint64_t screen_compete_legacy_bytes_sum;
    uint64_t screen_compete_screen_bytes_sum;
    uint64_t screen_prefilter_eval_count;
    uint64_t screen_prefilter_unique_sum;
    uint64_t screen_prefilter_avg_run_x100_sum;

    // Natural-specific row residual route telemetry.
    uint64_t natural_row_candidate_count;
    uint64_t natural_row_selected_count;
    uint64_t natural_row_rejected_cost_gate;
    uint64_t natural_row_build_fail_count;
    uint64_t natural_row_gain_bytes_sum;
    uint64_t natural_row_loss_bytes_sum;
    uint64_t natural_prefilter_eval_count;
    uint64_t natural_prefilter_pass_count;
    uint64_t natural_prefilter_reject_count;
    uint64_t natural_prefilter_unique_sum;
    uint64_t natural_prefilter_avg_run_x100_sum;
    uint64_t natural_prefilter_mad_x100_sum;
    uint64_t natural_prefilter_entropy_x100_sum;
    uint64_t route_compete_policy_skip_count;

    uint64_t palette_reorder_trials;
    uint64_t palette_reorder_adopted;
    uint64_t palette_reorder_gain_bytes_sum;

    uint64_t profile_ui_tiles;
    uint64_t profile_anime_tiles;
    uint64_t profile_photo_tiles;

    uint64_t class_eval_count;
    uint64_t class_copy_hit_x1000_sum;
    uint64_t class_mean_abs_diff_x1000_sum;
    uint64_t class_active_bins_sum;
    uint64_t anime_palette_bonus_applied;

    // Encode-side perf counters (nanoseconds), accumulated per encode call.
    uint64_t perf_encode_total_ns;
    uint64_t perf_encode_rgb_to_ycocg_ns;
    uint64_t perf_encode_profile_classify_ns;
    uint64_t perf_encode_plane_y_ns;
    uint64_t perf_encode_plane_co_ns;
    uint64_t perf_encode_plane_cg_ns;
    uint64_t perf_encode_container_pack_ns;

    uint64_t perf_encode_plane_total_ns;
    uint64_t perf_encode_plane_calls;
    uint64_t perf_encode_plane_pad_ns;
    uint64_t perf_encode_plane_block_classify_ns;
    uint64_t perf_encode_plane_filter_rows_ns;
    uint64_t perf_encode_plane_lo_stream_ns;
    uint64_t perf_encode_plane_hi_stream_ns;
    uint64_t perf_encode_plane_stream_wrap_ns;
    uint64_t perf_encode_plane_filter_ids_ns;
    uint64_t perf_encode_plane_pack_ns;
    uint64_t perf_encode_plane_route_compete_ns;

    LosslessModeDebugStats() { reset(); }
    void reset() { std::memset(this, 0, sizeof(*this)); }
    void accumulate_from(const LosslessModeDebugStats& other) {
        static_assert(sizeof(LosslessModeDebugStats) % sizeof(uint64_t) == 0,
                      "LosslessModeDebugStats must be uint64_t aligned");
        auto* dst = reinterpret_cast<uint64_t*>(this);
        const auto* src = reinterpret_cast<const uint64_t*>(&other);
        constexpr size_t kWords = sizeof(LosslessModeDebugStats) / sizeof(uint64_t);
        for (size_t i = 0; i < kWords; i++) dst[i] += src[i];
    }
};

} // namespace hakonyans
