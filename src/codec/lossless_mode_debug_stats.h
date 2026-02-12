#pragma once

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

    LosslessModeDebugStats() { reset(); }
    void reset() { std::memset(this, 0, sizeof(*this)); }
};

} // namespace hakonyans

