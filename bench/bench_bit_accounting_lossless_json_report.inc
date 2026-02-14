static double ratio_or_zero(uint64_t num, uint64_t den) {
    return (den > 0) ? (double)num / (double)den : 0.0;
}

void print_lossless_json(
    const std::string& image_path,
    int width,
    int height,
    const Accounting& a,
    const GrayscaleEncoder::LosslessModeDebugStats& s
) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "{\n";
    oss << "  \"image\": {\n";
    oss << "    \"path\": \"" << image_path << "\",\n";
    oss << "    \"width\": " << width << ",\n";
    oss << "    \"height\": " << height << "\n";
    oss << "  },\n";

    oss << "  \"lossless_accounting\": {\n";
    oss << "    \"total_file\": " << a.total_file << ",\n";
    oss << "    \"file_header\": " << a.file_header << ",\n";
    oss << "    \"chunk_dir\": " << a.chunk_dir << ",\n";
    oss << "    \"tile_header\": " << a.tile_header << ",\n";
    oss << "    \"filter_ids\": " << a.filter_ids << ",\n";
    oss << "    \"filter_lo\": " << a.filter_lo << ",\n";
    oss << "    \"filter_hi\": " << a.filter_hi << ",\n";
    oss << "    \"block_types\": " << a.block_types << ",\n";
    oss << "    \"palette\": " << a.palette << ",\n";
    oss << "    \"copy\": " << a.copy << ",\n";
    oss << "    \"tile4\": " << a.tile4 << ",\n";
    oss << "    \"screen_index\": " << a.screen_index << ",\n";
    oss << "    \"natural_row\": " << a.natural_row << ",\n";
    oss << "    \"unknown\": " << a.unknown << ",\n";
    oss << "    \"filter_lo_share\": " << ratio_or_zero(a.filter_lo, a.total_file) << ",\n";
    oss << "    \"filter_hi_share\": " << ratio_or_zero(a.filter_hi, a.total_file) << ",\n";
    oss << "    \"block_types_share\": " << ratio_or_zero(a.block_types, a.total_file) << "\n";
    oss << "  },\n";

    const uint64_t flo_total =
        s.filter_lo_mode0 + s.filter_lo_mode1 + s.filter_lo_mode2 +
        s.filter_lo_mode3 + s.filter_lo_mode4 + s.filter_lo_mode5 +
        s.filter_lo_mode6 + s.filter_lo_mode7 + s.filter_lo_mode8;
    const uint64_t fhi_total = s.filter_hi_sparse_count + s.filter_hi_dense_count;
    const uint64_t route_total = s.screen_candidate_count + s.natural_row_candidate_count;
    const uint64_t lzcost_rows = s.filter_rows_lzcost_rows_considered;
    const uint64_t lzcost_rejected = s.filter_rows_lzcost_rows_rejected_margin;

    oss << "  \"mode_stats\": {\n";
    oss << "    \"total_blocks\": " << s.total_blocks << ",\n";
    oss << "    \"selected\": {\n";
    oss << "      \"tile4\": " << s.tile4_selected << ",\n";
    oss << "      \"copy\": " << s.copy_selected << ",\n";
    oss << "      \"palette\": " << s.palette_selected << ",\n";
    oss << "      \"filter\": " << s.filter_selected << "\n";
    oss << "    },\n";
    oss << "    \"streams\": {\n";
    oss << "      \"block_types_bytes\": " << s.block_types_bytes_sum << ",\n";
    oss << "      \"filter_ids_raw_bytes\": " << s.filter_ids_raw_bytes_sum << ",\n";
    oss << "      \"filter_ids_compressed_bytes\": " << s.filter_ids_compressed_bytes_sum << ",\n";
    oss << "      \"filter_lo_raw_bytes\": " << s.filter_lo_raw_bytes_sum << ",\n";
    oss << "      \"filter_lo_compressed_bytes\": " << s.filter_lo_compressed_bytes_sum << ",\n";
    oss << "      \"filter_hi_raw_bytes\": " << s.filter_hi_raw_bytes_sum << ",\n";
    oss << "      \"filter_hi_compressed_bytes\": " << s.filter_hi_compressed_bytes_sum << ",\n";
    oss << "      \"filter_hi_sparse_count\": " << s.filter_hi_sparse_count << ",\n";
    oss << "      \"filter_hi_dense_count\": " << s.filter_hi_dense_count << "\n";
    oss << "    },\n";
    oss << "    \"filter_lo_modes\": {\n";
    oss << "      \"mode0\": " << s.filter_lo_mode0 << ",\n";
    oss << "      \"mode1\": " << s.filter_lo_mode1 << ",\n";
    oss << "      \"mode2\": " << s.filter_lo_mode2 << ",\n";
    oss << "      \"mode3\": " << s.filter_lo_mode3 << ",\n";
    oss << "      \"mode4\": " << s.filter_lo_mode4 << ",\n";
    oss << "      \"mode5\": " << s.filter_lo_mode5 << ",\n";
    oss << "      \"mode6\": " << s.filter_lo_mode6 << ",\n";
    oss << "      \"mode7\": " << s.filter_lo_mode7 << ",\n";
    oss << "      \"mode8\": " << s.filter_lo_mode8 << ",\n";
    oss << "      \"total\": " << flo_total << "\n";
    oss << "    },\n";
    oss << "    \"mode5\": {\n";
    oss << "      \"candidates\": " << s.filter_lo_mode5_candidates << ",\n";
    oss << "      \"reject_gate\": " << s.filter_lo_mode5_reject_gate << ",\n";
    oss << "      \"reject_best\": " << s.filter_lo_mode5_reject_best << ",\n";
    oss << "      \"avg_candidate_bytes\": " << ratio_or_zero(s.filter_lo_mode5_candidate_bytes_sum, s.filter_lo_mode5_candidates) << ",\n";
    oss << "      \"avg_wrapped_bytes\": " << ratio_or_zero(s.filter_lo_mode5_wrapped_bytes_sum, s.filter_lo_mode5_candidates) << ",\n";
    oss << "      \"avg_legacy_bytes\": " << ratio_or_zero(s.filter_lo_mode5_legacy_bytes_sum, s.filter_lo_mode5_candidates) << "\n";
    oss << "    },\n";
    oss << "    \"mode7\": {\n";
    oss << "      \"candidates\": " << s.filter_lo_mode7_candidates << ",\n";
    oss << "      \"reject_gate\": " << s.filter_lo_mode7_reject_gate << ",\n";
    oss << "      \"reject_best\": " << s.filter_lo_mode7_reject_best << ",\n";
    oss << "      \"avg_wrapped_bytes\": " << ratio_or_zero(s.filter_lo_mode7_wrapped_bytes_sum, s.filter_lo_mode7_candidates) << ",\n";
    oss << "      \"avg_legacy_bytes\": " << ratio_or_zero(s.filter_lo_mode7_legacy_bytes_sum, s.filter_lo_mode7_candidates) << ",\n";
    oss << "      \"avg_shared_ctx\": " << ratio_or_zero(s.filter_lo_mode7_shared_ctx_sum, s.filter_lo_mode7_candidates) << "\n";
    oss << "    },\n";
    oss << "    \"mode8\": {\n";
    oss << "      \"candidates\": " << s.filter_lo_mode8_candidates << ",\n";
    oss << "      \"reject_gate\": " << s.filter_lo_mode8_reject_gate << ",\n";
    oss << "      \"reject_best\": " << s.filter_lo_mode8_reject_best << ",\n";
    oss << "      \"avg_wrapped_bytes\": " << ratio_or_zero(s.filter_lo_mode8_wrapped_bytes_sum, s.filter_lo_mode8_candidates) << ",\n";
    oss << "      \"ctx_legacy_sum\": " << s.filter_lo_mode8_ctx_legacy_sum << ",\n";
    oss << "      \"ctx_delta_sum\": " << s.filter_lo_mode8_ctx_delta_sum << ",\n";
    oss << "      \"ctx_lz_sum\": " << s.filter_lo_mode8_ctx_lz_sum << "\n";
    oss << "    },\n";
    oss << "    \"route\": {\n";
    oss << "      \"screen_candidates\": " << s.screen_candidate_count << ",\n";
    oss << "      \"screen_selected\": " << s.screen_selected_count << ",\n";
    oss << "      \"screen_rejected_pre_gate\": " << s.screen_rejected_pre_gate << ",\n";
    oss << "      \"screen_rejected_cost_gate\": " << s.screen_rejected_cost_gate << ",\n";
    oss << "      \"natural_candidates\": " << s.natural_row_candidate_count << ",\n";
    oss << "      \"natural_selected\": " << s.natural_row_selected_count << ",\n";
    oss << "      \"natural_rejected_cost_gate\": " << s.natural_row_rejected_cost_gate << ",\n";
    oss << "      \"route_policy_skipped\": " << s.route_compete_policy_skip_count << ",\n";
    oss << "      \"route_candidate_total\": " << route_total << "\n";
    oss << "    },\n";
    oss << "    \"lzcost\": {\n";
    oss << "      \"rows_considered\": " << s.filter_rows_lzcost_rows_considered << ",\n";
    oss << "      \"rows_adopted\": " << s.filter_rows_lzcost_rows_adopted << ",\n";
    oss << "      \"rows_rejected_margin\": " << s.filter_rows_lzcost_rows_rejected_margin << ",\n";
    oss << "      \"adopt_rate\": " << ratio_or_zero(s.filter_rows_lzcost_rows_adopted, lzcost_rows) << ",\n";
    oss << "      \"reject_rate\": " << ratio_or_zero(lzcost_rejected, lzcost_rows) << ",\n";
    oss << "      \"base_cost_sum\": " << s.filter_rows_lzcost_base_cost_sum << ",\n";
    oss << "      \"best_cost_sum\": " << s.filter_rows_lzcost_best_cost_sum << "\n";
    oss << "    },\n";
    oss << "    \"profile\": {\n";
    oss << "      \"ui_tiles\": " << s.profile_ui_tiles << ",\n";
    oss << "      \"anime_tiles\": " << s.profile_anime_tiles << ",\n";
    oss << "      \"photo_tiles\": " << s.profile_photo_tiles << ",\n";
    oss << "      \"classifier_evals\": " << s.class_eval_count << "\n";
    oss << "    },\n";
    oss << "    \"derived\": {\n";
    oss << "      \"filter_lo_compression_ratio\": " << ratio_or_zero(s.filter_lo_compressed_bytes_sum, s.filter_lo_raw_bytes_sum) << ",\n";
    oss << "      \"filter_hi_compression_ratio\": " << ratio_or_zero(s.filter_hi_compressed_bytes_sum, s.filter_hi_raw_bytes_sum) << ",\n";
    oss << "      \"filter_ids_compression_ratio\": " << ratio_or_zero(s.filter_ids_compressed_bytes_sum, s.filter_ids_raw_bytes_sum) << ",\n";
    oss << "      \"filter_hi_sparse_ratio\": " << ratio_or_zero(s.filter_hi_sparse_count, fhi_total) << "\n";
    oss << "    }\n";
    oss << "  }\n";
    oss << "}\n";
    std::cout << oss.str();
}

