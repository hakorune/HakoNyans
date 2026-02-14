inline std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cols;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            cols.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    cols.push_back(cur);
    return cols;
}

inline std::map<std::string, BaselineRow> load_baseline_csv(const std::string& path) {
    std::map<std::string, BaselineRow> rows;
    if (path.empty()) return rows;

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open baseline CSV: " + path);
    }

    std::string line;
    bool first = true;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto cols = split_csv_line(line);
        if (first) {
            first = false;
            continue;
        }
        // image_id,image_name,width,height,hkn_bytes,png_bytes,png_over_hkn,dec_ms,natural_row_selected,natural_row_candidates,natural_row_selected_rate,gain_bytes,loss_bytes
        if (cols.size() < 13) continue;
        BaselineRow row;
        row.hkn_bytes = (size_t)std::stoull(cols[4]);
        row.png_over_hkn = std::stod(cols[6]);
        row.dec_ms = std::stod(cols[7]);
        row.natural_row_selected = (uint64_t)std::stoull(cols[8]);
        row.gain_bytes = (uint64_t)std::stoull(cols[11]);
        row.loss_bytes = (uint64_t)std::stoull(cols[12]);
        rows[cols[0]] = row;
    }
    return rows;
}

inline void write_results_csv(const std::string& path, const std::vector<ResultRow>& rows) {
    std::filesystem::path p(path);
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to write CSV: " + path);
    }

    ofs << "image_id,image_name,width,height,hkn_bytes,png_bytes,png_over_hkn,dec_ms,natural_row_selected,natural_row_candidates,natural_row_selected_rate,gain_bytes,loss_bytes,hkn_enc_images_per_s,hkn_dec_images_per_s,png_enc_images_per_s,png_dec_images_per_s,hkn_enc_cpu_over_wall,hkn_dec_cpu_over_wall,hkn_enc_ms,hkn_dec_ms,png_enc_ms,png_dec_ms,hkn_enc_rgb_to_ycocg_ms,hkn_enc_profile_ms,hkn_enc_plane_total_ms,hkn_enc_plane_block_classify_ms,hkn_enc_class_copy_shortcut_selected,hkn_enc_plane_filter_rows_ms,hkn_enc_plane_lo_stream_ms,hkn_enc_lo_mode2_eval_ms,hkn_enc_lo_mode3_eval_ms,hkn_enc_lo_mode4_eval_ms,hkn_enc_lo_mode5_eval_ms,hkn_enc_filter_lo_mode0,hkn_enc_filter_lo_mode1,hkn_enc_filter_lo_mode2,hkn_enc_filter_lo_mode3,hkn_enc_filter_lo_mode4,hkn_enc_filter_lo_mode5,hkn_enc_filter_rows_lzcost_eval_rows,hkn_enc_filter_rows_lzcost_topk_sum,hkn_enc_filter_rows_lzcost_paeth_selected,hkn_enc_filter_rows_lzcost_med_selected,hkn_enc_filter_rows_lzcost_rows_considered,hkn_enc_filter_rows_lzcost_rows_adopted,hkn_enc_filter_rows_lzcost_rows_rejected_margin,hkn_enc_filter_rows_lzcost_base_cost_sum,hkn_enc_filter_rows_lzcost_best_cost_sum,hkn_enc_filter_lo_mode6_candidates,hkn_enc_filter_lo_mode6_wrapped_bytes,hkn_enc_filter_lo_mode6_reject_gate,hkn_enc_filter_lo_mode6_reject_best,hkn_enc_lo_lz_probe_enabled,hkn_enc_lo_lz_probe_checked,hkn_enc_lo_lz_probe_pass,hkn_enc_lo_lz_probe_skip,hkn_enc_lo_lz_probe_sample_bytes,hkn_enc_lo_lz_probe_sample_lz_bytes,hkn_enc_lo_lz_probe_sample_wrapped_bytes,hkn_enc_plane_hi_stream_ms,hkn_enc_plane_stream_wrap_ms,hkn_enc_plane_route_ms,hkn_enc_plane_route_prefilter_ms,hkn_enc_plane_route_screen_candidate_ms,hkn_enc_plane_route_natural_candidate_ms,hkn_enc_plane_route_parallel,hkn_enc_plane_route_seq,hkn_enc_plane_route_parallel_tokens_sum,hkn_enc_route_nat_mode0_ms,hkn_enc_route_nat_mode1prep_ms,hkn_enc_route_nat_predpack_ms,hkn_enc_route_nat_mode1_ms,hkn_enc_route_nat_mode2_ms,hkn_enc_route_nat_mode3_ms,hkn_enc_route_nat_mode0_selected,hkn_enc_route_nat_mode1_selected,hkn_enc_route_nat_mode2_selected,hkn_enc_route_nat_mode3_selected,hkn_enc_route_nat_pred_raw,hkn_enc_route_nat_pred_rans,hkn_enc_route_nat_mode2_bias_adopt,hkn_enc_route_nat_mode2_bias_reject,hkn_enc_route_nat_mode2_lz_calls,hkn_enc_route_nat_mode2_lz_src_bytes,hkn_enc_route_nat_mode2_lz_out_bytes,hkn_enc_route_nat_mode2_lz_match_count,hkn_enc_route_nat_mode2_lz_match_bytes,hkn_enc_route_nat_mode2_lz_literal_bytes,hkn_enc_route_nat_mode2_lz_chain_steps,hkn_enc_route_nat_mode2_lz_depth_limit_hits,hkn_enc_route_nat_mode2_lz_early_maxlen_hits,hkn_enc_route_nat_mode2_lz_nice_cutoff_hits,hkn_enc_route_nat_mode2_lz_len3_reject_dist,hkn_enc_route_nat_prep_parallel,hkn_enc_route_nat_prep_seq,hkn_enc_route_nat_prep_tokens_sum,hkn_enc_route_nat_mode12_parallel,hkn_enc_route_nat_mode12_seq,hkn_enc_route_nat_mode12_tokens_sum,hkn_enc_container_pack_ms,hkn_dec_header_ms,hkn_dec_plane_total_ms,hkn_dec_ycocg_to_rgb_ms,hkn_dec_plane_dispatch_ms,hkn_dec_plane_wait_ms,hkn_dec_ycocg_dispatch_ms,hkn_dec_ycocg_kernel_ms,hkn_dec_ycocg_wait_ms,hkn_dec_ycocg_rows_sum,hkn_dec_ycocg_pixels_sum,hkn_dec_plane_try_natural_ms,hkn_dec_plane_screen_wrapper_ms,hkn_dec_plane_block_types_ms,hkn_dec_plane_filter_ids_ms,hkn_dec_plane_filter_lo_ms,hkn_dec_plane_filter_hi_ms,hkn_dec_plane_reconstruct_ms,hkn_enc_plane_y_ms,hkn_enc_plane_co_ms,hkn_enc_plane_cg_ms,hkn_dec_plane_y_ms,hkn_dec_plane_co_ms,hkn_dec_plane_cg_ms,hkn_enc_plane_parallel_3way,hkn_enc_plane_parallel_2way,hkn_enc_plane_parallel_seq,hkn_enc_plane_parallel_tokens_sum,hkn_dec_plane_parallel_3way,hkn_dec_plane_parallel_seq,hkn_dec_plane_parallel_tokens_sum,hkn_dec_ycocg_parallel,hkn_dec_ycocg_sequential,hkn_dec_ycocg_parallel_threads_sum,hkn_dec_filter_lo_mode_raw,hkn_dec_filter_lo_mode1,hkn_dec_filter_lo_mode2,hkn_dec_filter_lo_mode3,hkn_dec_filter_lo_mode4,hkn_dec_filter_lo_mode5,hkn_dec_filter_lo_mode_invalid,hkn_dec_filter_lo_fallback_zero_fill,hkn_dec_filter_lo_mode4_parallel_tiles,hkn_dec_filter_lo_mode4_sequential_tiles,hkn_dec_filter_lo_decode_rans_ms,hkn_dec_filter_lo_decode_shared_rans_ms,hkn_dec_filter_lo_tilelz_ms,hkn_dec_recon_copy_fast_rows,hkn_dec_recon_copy_slow_rows,hkn_dec_recon_tile4_fast_quads,hkn_dec_recon_tile4_slow_quads,hkn_dec_recon_residual_missing\n";
    ofs << std::fixed << std::setprecision(6);
    for (const auto& r : rows) {
        ofs << r.image_id << ","
            << r.image_name << ","
            << r.width << ","
            << r.height << ","
            << r.hkn_bytes << ","
            << r.png_bytes << ","
            << r.png_over_hkn << ","
            << r.dec_ms << ","
            << r.natural_row_selected << ","
            << r.natural_row_candidates << ","
            << r.natural_row_selected_rate << ","
            << r.gain_bytes << ","
            << r.loss_bytes << ","
            << r.hkn_enc_images_per_s << ","
            << r.hkn_dec_images_per_s << ","
            << r.png_enc_images_per_s << ","
            << r.png_dec_images_per_s << ","
            << r.hkn_enc_cpu_over_wall << ","
            << r.hkn_dec_cpu_over_wall << ","
            << r.hkn_enc_ms << ","
            << r.hkn_dec_ms << ","
            << r.png_enc_ms << ","
            << r.png_dec_ms << ","
            << r.hkn_enc_rgb_to_ycocg_ms << ","
            << r.hkn_enc_profile_ms << ","
            << r.hkn_enc_plane_total_ms << ","
            << r.hkn_enc_plane_block_classify_ms << ","
            << r.hkn_enc_class_copy_shortcut_selected << ","
            << r.hkn_enc_plane_filter_rows_ms << ","
            << r.hkn_enc_plane_lo_stream_ms << ","
            << r.hkn_enc_lo_mode2_eval_ms << ","
            << r.hkn_enc_lo_mode3_eval_ms << ","
            << r.hkn_enc_lo_mode4_eval_ms << ","
            << r.hkn_enc_lo_mode5_eval_ms << ","
            << r.hkn_enc_filter_lo_mode0 << ","
            << r.hkn_enc_filter_lo_mode1 << ","
            << r.hkn_enc_filter_lo_mode2 << ","
            << r.hkn_enc_filter_lo_mode3 << ","
            << r.hkn_enc_filter_lo_mode4 << ","
            << r.hkn_enc_filter_lo_mode5 << ","
            << r.hkn_enc_filter_rows_lzcost_eval_rows << ","
            << r.hkn_enc_filter_rows_lzcost_topk_sum << ","
            << r.hkn_enc_filter_rows_lzcost_paeth_selected << ","
            << r.hkn_enc_filter_rows_lzcost_med_selected << ","
            << r.hkn_enc_filter_rows_lzcost_rows_considered << ","
            << r.hkn_enc_filter_rows_lzcost_rows_adopted << ","
            << r.hkn_enc_filter_rows_lzcost_rows_rejected_margin << ","
            << r.hkn_enc_filter_rows_lzcost_base_cost_sum << ","
            << r.hkn_enc_filter_rows_lzcost_best_cost_sum << ","
            << r.hkn_enc_filter_lo_mode6_candidates << ","
            << r.hkn_enc_filter_lo_mode6_wrapped_bytes << ","
            << r.hkn_enc_filter_lo_mode6_reject_gate << ","
            << r.hkn_enc_filter_lo_mode6_reject_best << ","
            << r.hkn_enc_lo_lz_probe_enabled << ","
            << r.hkn_enc_lo_lz_probe_checked << ","
            << r.hkn_enc_lo_lz_probe_pass << ","
            << r.hkn_enc_lo_lz_probe_skip << ","
            << r.hkn_enc_lo_lz_probe_sample_bytes << ","
            << r.hkn_enc_lo_lz_probe_sample_lz_bytes << ","
            << r.hkn_enc_lo_lz_probe_sample_wrapped_bytes << ","
            << r.hkn_enc_plane_hi_stream_ms << ","
            << r.hkn_enc_plane_stream_wrap_ms << ","
            << r.hkn_enc_plane_route_ms << ","
            << r.hkn_enc_plane_route_prefilter_ms << ","
            << r.hkn_enc_plane_route_screen_candidate_ms << ","
            << r.hkn_enc_plane_route_natural_candidate_ms << ","
            << r.hkn_enc_plane_route_parallel << ","
            << r.hkn_enc_plane_route_seq << ","
            << r.hkn_enc_plane_route_parallel_tokens_sum << ","
            << r.hkn_enc_route_nat_mode0_ms << ","
            << r.hkn_enc_route_nat_mode1prep_ms << ","
            << r.hkn_enc_route_nat_predpack_ms << ","
            << r.hkn_enc_route_nat_mode1_ms << ","
            << r.hkn_enc_route_nat_mode2_ms << ","
            << r.hkn_enc_route_nat_mode3_ms << ","
            << r.hkn_enc_route_nat_mode0_selected << ","
            << r.hkn_enc_route_nat_mode1_selected << ","
            << r.hkn_enc_route_nat_mode2_selected << ","
            << r.hkn_enc_route_nat_mode3_selected << ","
            << r.hkn_enc_route_nat_pred_raw << ","
            << r.hkn_enc_route_nat_pred_rans << ","
            << r.hkn_enc_route_nat_mode2_bias_adopt << ","
            << r.hkn_enc_route_nat_mode2_bias_reject << ","
            << r.hkn_enc_route_nat_mode2_lz_calls << ","
            << r.hkn_enc_route_nat_mode2_lz_src_bytes << ","
            << r.hkn_enc_route_nat_mode2_lz_out_bytes << ","
            << r.hkn_enc_route_nat_mode2_lz_match_count << ","
            << r.hkn_enc_route_nat_mode2_lz_match_bytes << ","
            << r.hkn_enc_route_nat_mode2_lz_literal_bytes << ","
            << r.hkn_enc_route_nat_mode2_lz_chain_steps << ","
            << r.hkn_enc_route_nat_mode2_lz_depth_limit_hits << ","
            << r.hkn_enc_route_nat_mode2_lz_early_maxlen_hits << ","
            << r.hkn_enc_route_nat_mode2_lz_nice_cutoff_hits << ","
            << r.hkn_enc_route_nat_mode2_lz_len3_reject_dist << ","
            << r.hkn_enc_route_nat_prep_parallel << ","
            << r.hkn_enc_route_nat_prep_seq << ","
            << r.hkn_enc_route_nat_prep_tokens_sum << ","
            << r.hkn_enc_route_nat_mode12_parallel << ","
            << r.hkn_enc_route_nat_mode12_seq << ","
            << r.hkn_enc_route_nat_mode12_tokens_sum << ","
            << r.hkn_enc_container_pack_ms << ","
            << r.hkn_dec_header_ms << ","
            << r.hkn_dec_plane_total_ms << ","
            << r.hkn_dec_ycocg_to_rgb_ms << ","
            << r.hkn_dec_plane_dispatch_ms << ","
            << r.hkn_dec_plane_wait_ms << ","
            << r.hkn_dec_ycocg_dispatch_ms << ","
            << r.hkn_dec_ycocg_kernel_ms << ","
            << r.hkn_dec_ycocg_wait_ms << ","
            << r.hkn_dec_ycocg_rows_sum << ","
            << r.hkn_dec_ycocg_pixels_sum << ","
            << r.hkn_dec_plane_try_natural_ms << ","
            << r.hkn_dec_plane_screen_wrapper_ms << ","
            << r.hkn_dec_plane_block_types_ms << ","
            << r.hkn_dec_plane_filter_ids_ms << ","
            << r.hkn_dec_plane_filter_lo_ms << ","
            << r.hkn_dec_plane_filter_hi_ms << ","
            << r.hkn_dec_plane_reconstruct_ms << ","
            << r.hkn_enc_plane_y_ms << ","
            << r.hkn_enc_plane_co_ms << ","
            << r.hkn_enc_plane_cg_ms << ","
            << r.hkn_dec_plane_y_ms << ","
            << r.hkn_dec_plane_co_ms << ","
            << r.hkn_dec_plane_cg_ms << ","
            << r.hkn_enc_plane_parallel_3way << ","
            << r.hkn_enc_plane_parallel_2way << ","
            << r.hkn_enc_plane_parallel_seq << ","
            << r.hkn_enc_plane_parallel_tokens_sum << ","
            << r.hkn_dec_plane_parallel_3way << ","
            << r.hkn_dec_plane_parallel_seq << ","
            << r.hkn_dec_plane_parallel_tokens_sum << ","
            << r.hkn_dec_ycocg_parallel << ","
            << r.hkn_dec_ycocg_sequential << ","
            << r.hkn_dec_ycocg_parallel_threads_sum << ","
            << r.hkn_dec_filter_lo_mode_raw << ","
            << r.hkn_dec_filter_lo_mode1 << ","
            << r.hkn_dec_filter_lo_mode2 << ","
            << r.hkn_dec_filter_lo_mode3 << ","
            << r.hkn_dec_filter_lo_mode4 << ","
            << r.hkn_dec_filter_lo_mode5 << ","
            << r.hkn_dec_filter_lo_mode_invalid << ","
            << r.hkn_dec_filter_lo_fallback_zero_fill << ","
            << r.hkn_dec_filter_lo_mode4_parallel_tiles << ","
            << r.hkn_dec_filter_lo_mode4_sequential_tiles << ","
            << r.hkn_dec_filter_lo_decode_rans_ms << ","
            << r.hkn_dec_filter_lo_decode_shared_rans_ms << ","
            << r.hkn_dec_filter_lo_tilelz_ms << ","
            << r.hkn_dec_recon_copy_fast_rows << ","
            << r.hkn_dec_recon_copy_slow_rows << ","
            << r.hkn_dec_recon_tile4_fast_quads << ","
            << r.hkn_dec_recon_tile4_slow_quads << ","
            << r.hkn_dec_recon_residual_missing << "\n";
    }
}

