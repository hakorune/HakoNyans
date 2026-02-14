struct EvalImage {
    std::string rel_path;
    std::string name;
};

inline const std::vector<EvalImage> kFixedEvalSet = {
    {"kodak/kodim01.ppm", "kodim01"},
    {"kodak/kodim02.ppm", "kodim02"},
    {"kodak/kodim03.ppm", "kodim03"},
    {"kodak/hd_01.ppm", "hd_01"},
    {"photo/nature_01.ppm", "nature_01"},
    {"photo/nature_02.ppm", "nature_02"},
};

struct Args {
    std::string base_dir = "test_images";
    std::string out_csv = "bench_results/phase9w_current.csv";
    std::string baseline_csv;
    int warmup = 1;
    int runs = 3;
    hakonyans::GrayscaleEncoder::LosslessPreset preset = hakonyans::GrayscaleEncoder::LosslessPreset::BALANCED;
};

struct ResultRow {
    std::string image_id;
    std::string image_name;
    int width = 0;
    int height = 0;

    size_t hkn_bytes = 0;
    size_t png_bytes = 0;
    double png_over_hkn = 0.0;

    double dec_ms = 0.0; // Median
    double hkn_enc_ms = 0.0; // Median
    double hkn_dec_ms = 0.0; // Median (same value as dec_ms for compatibility)
    double png_enc_ms = 0.0; // Median
    double png_dec_ms = 0.0; // Median
    double hkn_enc_images_per_s = 0.0;
    double hkn_dec_images_per_s = 0.0;
    double png_enc_images_per_s = 0.0;
    double png_dec_images_per_s = 0.0;
    double hkn_enc_cpu_over_wall = 0.0;
    double hkn_dec_cpu_over_wall = 0.0;

    // HKN encode stage timings (Median)
    double hkn_enc_rgb_to_ycocg_ms = 0.0;
    double hkn_enc_profile_ms = 0.0;
    double hkn_enc_plane_total_ms = 0.0;
    double hkn_enc_plane_block_classify_ms = 0.0;
    uint64_t hkn_enc_class_copy_shortcut_selected = 0;
    double hkn_enc_plane_filter_rows_ms = 0.0;
    double hkn_enc_plane_lo_stream_ms = 0.0;
    double hkn_enc_lo_mode2_eval_ms = 0.0;
    double hkn_enc_lo_mode3_eval_ms = 0.0;
    double hkn_enc_lo_mode4_eval_ms = 0.0;
    double hkn_enc_lo_mode5_eval_ms = 0.0;
    uint64_t hkn_enc_filter_lo_mode0 = 0;
    uint64_t hkn_enc_filter_lo_mode1 = 0;
    uint64_t hkn_enc_filter_lo_mode2 = 0;
    uint64_t hkn_enc_filter_lo_mode3 = 0;
    uint64_t hkn_enc_filter_lo_mode4 = 0;
    uint64_t hkn_enc_filter_lo_mode5 = 0;
    // LZCOST filter row telemetry (Phase 9X-3/4)
    uint64_t hkn_enc_filter_rows_lzcost_eval_rows = 0;
    uint64_t hkn_enc_filter_rows_lzcost_topk_sum = 0;
    uint64_t hkn_enc_filter_rows_lzcost_paeth_selected = 0;
    uint64_t hkn_enc_filter_rows_lzcost_med_selected = 0;
    uint64_t hkn_enc_filter_rows_lzcost_rows_considered = 0;
    uint64_t hkn_enc_filter_rows_lzcost_rows_adopted = 0;
    uint64_t hkn_enc_filter_rows_lzcost_rows_rejected_margin = 0;
    uint64_t hkn_enc_filter_rows_lzcost_base_cost_sum = 0;
    uint64_t hkn_enc_filter_rows_lzcost_best_cost_sum = 0;
    // Mode 6 telemetry (Phase 9X-2)
    uint64_t hkn_enc_filter_lo_mode6_candidates = 0;
    uint64_t hkn_enc_filter_lo_mode6_wrapped_bytes = 0;
    uint64_t hkn_enc_filter_lo_mode6_reject_gate = 0;
    uint64_t hkn_enc_filter_lo_mode6_reject_best = 0;
    uint64_t hkn_enc_lo_lz_probe_enabled = 0;
    uint64_t hkn_enc_lo_lz_probe_checked = 0;
    uint64_t hkn_enc_lo_lz_probe_pass = 0;
    uint64_t hkn_enc_lo_lz_probe_skip = 0;
    uint64_t hkn_enc_lo_lz_probe_sample_bytes = 0;
    uint64_t hkn_enc_lo_lz_probe_sample_lz_bytes = 0;
    uint64_t hkn_enc_lo_lz_probe_sample_wrapped_bytes = 0;
    double hkn_enc_plane_hi_stream_ms = 0.0;
    double hkn_enc_plane_stream_wrap_ms = 0.0;
    double hkn_enc_plane_route_ms = 0.0;
    double hkn_enc_plane_route_prefilter_ms = 0.0;
    double hkn_enc_plane_route_screen_candidate_ms = 0.0;
    double hkn_enc_plane_route_natural_candidate_ms = 0.0;
    uint64_t hkn_enc_plane_route_parallel = 0;
    uint64_t hkn_enc_plane_route_seq = 0;
    uint64_t hkn_enc_plane_route_parallel_tokens_sum = 0;
    double hkn_enc_route_nat_mode0_ms = 0.0;
    double hkn_enc_route_nat_mode1prep_ms = 0.0;
    double hkn_enc_route_nat_predpack_ms = 0.0;
    double hkn_enc_route_nat_mode1_ms = 0.0;
    double hkn_enc_route_nat_mode2_ms = 0.0;
    double hkn_enc_route_nat_mode3_ms = 0.0;
    uint64_t hkn_enc_route_nat_mode0_selected = 0;
    uint64_t hkn_enc_route_nat_mode1_selected = 0;
    uint64_t hkn_enc_route_nat_mode2_selected = 0;
    uint64_t hkn_enc_route_nat_mode3_selected = 0;
    uint64_t hkn_enc_route_nat_pred_raw = 0;
    uint64_t hkn_enc_route_nat_pred_rans = 0;
    uint64_t hkn_enc_route_nat_mode2_bias_adopt = 0;
    uint64_t hkn_enc_route_nat_mode2_bias_reject = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_calls = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_src_bytes = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_out_bytes = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_match_count = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_match_bytes = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_literal_bytes = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_chain_steps = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_depth_limit_hits = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_early_maxlen_hits = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_nice_cutoff_hits = 0;
    uint64_t hkn_enc_route_nat_mode2_lz_len3_reject_dist = 0;
    uint64_t hkn_enc_route_nat_prep_parallel = 0;
    uint64_t hkn_enc_route_nat_prep_seq = 0;
    uint64_t hkn_enc_route_nat_prep_tokens_sum = 0;
    uint64_t hkn_enc_route_nat_mode12_parallel = 0;
    uint64_t hkn_enc_route_nat_mode12_seq = 0;
    uint64_t hkn_enc_route_nat_mode12_tokens_sum = 0;
    double hkn_enc_container_pack_ms = 0.0;
    double hkn_enc_plane_y_ms = 0.0;
    double hkn_enc_plane_co_ms = 0.0;
    double hkn_enc_plane_cg_ms = 0.0;
    uint64_t hkn_enc_plane_parallel_3way = 0;
    uint64_t hkn_enc_plane_parallel_2way = 0;
    uint64_t hkn_enc_plane_parallel_seq = 0;
    uint64_t hkn_enc_plane_parallel_tokens_sum = 0;

    // HKN decode stage timings (Median)
    double hkn_dec_header_ms = 0.0;
    double hkn_dec_plane_total_ms = 0.0;
    double hkn_dec_ycocg_to_rgb_ms = 0.0;
    double hkn_dec_plane_dispatch_ms = 0.0;
    double hkn_dec_plane_wait_ms = 0.0;
    double hkn_dec_ycocg_dispatch_ms = 0.0;
    double hkn_dec_ycocg_kernel_ms = 0.0;
    double hkn_dec_ycocg_wait_ms = 0.0;
    uint64_t hkn_dec_ycocg_rows_sum = 0;
    uint64_t hkn_dec_ycocg_pixels_sum = 0;
    double hkn_dec_plane_try_natural_ms = 0.0;
    double hkn_dec_plane_screen_wrapper_ms = 0.0;
    double hkn_dec_plane_block_types_ms = 0.0;
    double hkn_dec_plane_filter_ids_ms = 0.0;
    double hkn_dec_plane_filter_lo_ms = 0.0;
    double hkn_dec_plane_filter_hi_ms = 0.0;
    double hkn_dec_plane_reconstruct_ms = 0.0;
    double hkn_dec_plane_y_ms = 0.0;
    double hkn_dec_plane_co_ms = 0.0;
    double hkn_dec_plane_cg_ms = 0.0;
    uint64_t hkn_dec_plane_parallel_3way = 0;
    uint64_t hkn_dec_plane_parallel_seq = 0;
    uint64_t hkn_dec_plane_parallel_tokens_sum = 0;
    uint64_t hkn_dec_ycocg_parallel = 0;
    uint64_t hkn_dec_ycocg_sequential = 0;
    uint64_t hkn_dec_ycocg_parallel_threads_sum = 0;
    uint64_t hkn_dec_filter_lo_mode_raw = 0;
    uint64_t hkn_dec_filter_lo_mode1 = 0;
    uint64_t hkn_dec_filter_lo_mode2 = 0;
    uint64_t hkn_dec_filter_lo_mode3 = 0;
    uint64_t hkn_dec_filter_lo_mode4 = 0;
    uint64_t hkn_dec_filter_lo_mode5 = 0;
    uint64_t hkn_dec_filter_lo_mode_invalid = 0;
    uint64_t hkn_dec_filter_lo_fallback_zero_fill = 0;
    uint64_t hkn_dec_filter_lo_mode4_parallel_tiles = 0;
    uint64_t hkn_dec_filter_lo_mode4_sequential_tiles = 0;
    double hkn_dec_filter_lo_decode_rans_ms = 0.0;
    double hkn_dec_filter_lo_decode_shared_rans_ms = 0.0;
    double hkn_dec_filter_lo_tilelz_ms = 0.0;
    uint64_t hkn_dec_recon_copy_fast_rows = 0;
    uint64_t hkn_dec_recon_copy_slow_rows = 0;
    uint64_t hkn_dec_recon_tile4_fast_quads = 0;
    uint64_t hkn_dec_recon_tile4_slow_quads = 0;
    uint64_t hkn_dec_recon_residual_missing = 0;

    uint64_t natural_row_selected = 0;
    uint64_t natural_row_candidates = 0;
    double natural_row_selected_rate = 0.0;

    uint64_t gain_bytes = 0;
    uint64_t loss_bytes = 0;
};

struct BaselineRow {
    size_t hkn_bytes = 0;
    double dec_ms = 0.0;
    uint64_t natural_row_selected = 0;
    uint64_t gain_bytes = 0;
    uint64_t loss_bytes = 0;
    double png_over_hkn = 0.0;
};

