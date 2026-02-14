    for (int i = 0; i < args.warmup + args.runs; i++) {
        auto hkn_t0 = std::chrono::steady_clock::now();
        auto hkn = GrayscaleEncoder::encode_color_lossless(
            ppm.rgb_data.data(),
            (uint32_t)ppm.width,
            (uint32_t)ppm.height,
            args.preset
        );
        auto hkn_t1 = std::chrono::steady_clock::now();
        double hkn_enc_ms = std::chrono::duration<double, std::milli>(hkn_t1 - hkn_t0).count();
        auto enc_stats = GrayscaleEncoder::get_lossless_mode_debug_stats();

        int dec_w = 0;
        int dec_h = 0;
        auto t0 = std::chrono::steady_clock::now();
        auto dec = GrayscaleDecoder::decode_color_lossless(hkn, dec_w, dec_h);
        auto t1 = std::chrono::steady_clock::now();
        double hkn_dec_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        auto dec_stats = GrayscaleDecoder::get_lossless_decode_debug_stats();

        if (dec_w != ppm.width || dec_h != ppm.height || dec != ppm.rgb_data) {
            throw std::runtime_error("Lossless roundtrip failed for " + img.rel_path);
        }

        auto png_enc = encode_png(ppm.rgb_data.data(), ppm.width, ppm.height);
        auto png_dec = decode_png(png_enc.png_data.data(), png_enc.png_data.size());
        if (png_dec.width != ppm.width || png_dec.height != ppm.height) {
            throw std::runtime_error("PNG roundtrip failed for " + img.rel_path);
        }

        if (i >= args.warmup) {
            hkn_size_samples.push_back(hkn.size());
            png_size_samples.push_back(png_enc.png_data.size());
            hkn_enc_samples_ms.push_back(hkn_enc_ms);
            hkn_dec_samples_ms.push_back(hkn_dec_ms);
            png_enc_samples_ms.push_back(png_enc.encode_time_ms);
            png_dec_samples_ms.push_back(png_dec.decode_time_ms);
            hkn_enc_rgb_to_ycocg_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_rgb_to_ycocg_ns));
            hkn_enc_profile_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_profile_classify_ns));
            hkn_enc_plane_total_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_total_ns));
            hkn_enc_plane_block_classify_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_block_classify_ns));
            hkn_enc_class_copy_shortcut_selected_samples.push_back(
                enc_stats.class_copy_shortcut_selected
            );
            hkn_enc_plane_filter_rows_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_filter_rows_ns));
            hkn_enc_plane_lo_stream_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_lo_stream_ns));
            hkn_enc_lo_mode2_eval_samples_ms.push_back(ns_to_ms(enc_stats.filter_lo_mode2_eval_ns));
            hkn_enc_lo_mode3_eval_samples_ms.push_back(ns_to_ms(enc_stats.filter_lo_mode3_eval_ns));
            hkn_enc_lo_mode4_eval_samples_ms.push_back(ns_to_ms(enc_stats.filter_lo_mode4_eval_ns));
            hkn_enc_lo_mode5_eval_samples_ms.push_back(ns_to_ms(enc_stats.filter_lo_mode5_eval_ns));
            hkn_enc_filter_lo_mode0_samples.push_back(enc_stats.filter_lo_mode0);
            hkn_enc_filter_lo_mode1_samples.push_back(enc_stats.filter_lo_mode1);
            hkn_enc_filter_lo_mode2_samples.push_back(enc_stats.filter_lo_mode2);
            hkn_enc_filter_lo_mode3_samples.push_back(enc_stats.filter_lo_mode3);
            hkn_enc_filter_lo_mode4_samples.push_back(enc_stats.filter_lo_mode4);
            hkn_enc_filter_lo_mode5_samples.push_back(enc_stats.filter_lo_mode5);
            hkn_enc_filter_rows_lzcost_eval_rows_samples.push_back(enc_stats.filter_rows_lzcost_eval_rows);
            hkn_enc_filter_rows_lzcost_topk_sum_samples.push_back(enc_stats.filter_rows_lzcost_topk_sum);
            hkn_enc_filter_rows_lzcost_paeth_selected_samples.push_back(enc_stats.filter_rows_lzcost_paeth_selected);
            hkn_enc_filter_rows_lzcost_med_selected_samples.push_back(enc_stats.filter_rows_lzcost_med_selected);
            hkn_enc_filter_rows_lzcost_rows_considered_samples.push_back(enc_stats.filter_rows_lzcost_rows_considered);
            hkn_enc_filter_rows_lzcost_rows_adopted_samples.push_back(enc_stats.filter_rows_lzcost_rows_adopted);
            hkn_enc_filter_rows_lzcost_rows_rejected_margin_samples.push_back(enc_stats.filter_rows_lzcost_rows_rejected_margin);
            hkn_enc_filter_rows_lzcost_base_cost_sum_samples.push_back(enc_stats.filter_rows_lzcost_base_cost_sum);
            hkn_enc_filter_rows_lzcost_best_cost_sum_samples.push_back(enc_stats.filter_rows_lzcost_best_cost_sum);
            hkn_enc_filter_lo_mode6_candidates_samples.push_back(enc_stats.filter_lo_mode6_candidates);
            hkn_enc_filter_lo_mode6_wrapped_bytes_samples.push_back(enc_stats.filter_lo_mode6_wrapped_bytes_sum);
            hkn_enc_filter_lo_mode6_reject_gate_samples.push_back(enc_stats.filter_lo_mode6_reject_gate);
            hkn_enc_filter_lo_mode6_reject_best_samples.push_back(enc_stats.filter_lo_mode6_reject_best);
            hkn_enc_lo_lz_probe_enabled_samples.push_back(enc_stats.filter_lo_lz_probe_enabled);
            hkn_enc_lo_lz_probe_checked_samples.push_back(enc_stats.filter_lo_lz_probe_checked);
            hkn_enc_lo_lz_probe_pass_samples.push_back(enc_stats.filter_lo_lz_probe_pass);
            hkn_enc_lo_lz_probe_skip_samples.push_back(enc_stats.filter_lo_lz_probe_skip);
            hkn_enc_lo_lz_probe_sample_bytes_samples.push_back(
                enc_stats.filter_lo_lz_probe_sample_bytes_sum
            );
            hkn_enc_lo_lz_probe_sample_lz_bytes_samples.push_back(
                enc_stats.filter_lo_lz_probe_sample_lz_bytes_sum
            );
            hkn_enc_lo_lz_probe_sample_wrapped_bytes_samples.push_back(
                enc_stats.filter_lo_lz_probe_sample_wrapped_bytes_sum
            );
            hkn_enc_plane_hi_stream_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_hi_stream_ns));
            hkn_enc_plane_stream_wrap_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_stream_wrap_ns));
            hkn_enc_plane_route_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_route_compete_ns));
            hkn_enc_plane_route_prefilter_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_route_prefilter_ns));
            hkn_enc_plane_route_screen_candidate_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_route_screen_candidate_ns));
            hkn_enc_plane_route_natural_candidate_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_route_natural_candidate_ns));
            hkn_enc_plane_route_parallel_samples.push_back(enc_stats.perf_encode_plane_route_parallel_count);
            hkn_enc_plane_route_seq_samples.push_back(enc_stats.perf_encode_plane_route_seq_count);
            hkn_enc_plane_route_parallel_tokens_sum_samples.push_back(enc_stats.perf_encode_plane_route_parallel_tokens_sum);
            hkn_enc_route_nat_mode0_samples_ms.push_back(ns_to_ms(enc_stats.natural_row_mode0_build_ns));
            hkn_enc_route_nat_mode1prep_samples_ms.push_back(ns_to_ms(enc_stats.natural_row_mode1_prepare_ns));
            hkn_enc_route_nat_predpack_samples_ms.push_back(ns_to_ms(enc_stats.natural_row_pred_pack_ns));
            hkn_enc_route_nat_mode1_samples_ms.push_back(ns_to_ms(enc_stats.natural_row_mode1_build_ns));
            hkn_enc_route_nat_mode2_samples_ms.push_back(ns_to_ms(enc_stats.natural_row_mode2_build_ns));
            hkn_enc_route_nat_mode3_samples_ms.push_back(ns_to_ms(enc_stats.natural_row_mode3_build_ns));
            hkn_enc_route_nat_mode0_selected_samples.push_back(enc_stats.natural_row_mode0_selected_count);
            hkn_enc_route_nat_mode1_selected_samples.push_back(enc_stats.natural_row_mode1_selected_count);
            hkn_enc_route_nat_mode2_selected_samples.push_back(enc_stats.natural_row_mode2_selected_count);
            hkn_enc_route_nat_mode3_selected_samples.push_back(enc_stats.natural_row_mode3_selected_count);
            hkn_enc_route_nat_pred_raw_samples.push_back(enc_stats.natural_row_pred_mode_raw_count);
            hkn_enc_route_nat_pred_rans_samples.push_back(enc_stats.natural_row_pred_mode_rans_count);
            hkn_enc_route_nat_mode2_bias_adopt_samples.push_back(enc_stats.natural_row_mode2_bias_adopt_count);
            hkn_enc_route_nat_mode2_bias_reject_samples.push_back(enc_stats.natural_row_mode2_bias_reject_count);
            hkn_enc_route_nat_mode2_lz_calls_samples.push_back(enc_stats.natural_row_mode2_lz_calls);
            hkn_enc_route_nat_mode2_lz_src_bytes_samples.push_back(enc_stats.natural_row_mode2_lz_src_bytes_sum);
            hkn_enc_route_nat_mode2_lz_out_bytes_samples.push_back(enc_stats.natural_row_mode2_lz_out_bytes_sum);
            hkn_enc_route_nat_mode2_lz_match_count_samples.push_back(enc_stats.natural_row_mode2_lz_match_count);
            hkn_enc_route_nat_mode2_lz_match_bytes_samples.push_back(enc_stats.natural_row_mode2_lz_match_bytes_sum);
            hkn_enc_route_nat_mode2_lz_literal_bytes_samples.push_back(enc_stats.natural_row_mode2_lz_literal_bytes_sum);
            hkn_enc_route_nat_mode2_lz_chain_steps_samples.push_back(enc_stats.natural_row_mode2_lz_chain_steps_sum);
            hkn_enc_route_nat_mode2_lz_depth_limit_hits_samples.push_back(enc_stats.natural_row_mode2_lz_depth_limit_hits);
            hkn_enc_route_nat_mode2_lz_early_maxlen_hits_samples.push_back(enc_stats.natural_row_mode2_lz_early_maxlen_hits);
            hkn_enc_route_nat_mode2_lz_nice_cutoff_hits_samples.push_back(enc_stats.natural_row_mode2_lz_nice_cutoff_hits);
            hkn_enc_route_nat_mode2_lz_len3_reject_dist_samples.push_back(enc_stats.natural_row_mode2_lz_len3_reject_dist);
            hkn_enc_route_nat_prep_parallel_samples.push_back(enc_stats.natural_row_prep_parallel_count);
            hkn_enc_route_nat_prep_seq_samples.push_back(enc_stats.natural_row_prep_seq_count);
            hkn_enc_route_nat_prep_tokens_sum_samples.push_back(enc_stats.natural_row_prep_parallel_tokens_sum);
            hkn_enc_route_nat_mode12_parallel_samples.push_back(enc_stats.natural_row_mode12_parallel_count);
            hkn_enc_route_nat_mode12_seq_samples.push_back(enc_stats.natural_row_mode12_seq_count);
            hkn_enc_route_nat_mode12_tokens_sum_samples.push_back(enc_stats.natural_row_mode12_parallel_tokens_sum);
            hkn_enc_container_pack_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_container_pack_ns));
            hkn_enc_plane_y_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_y_ns));
            hkn_enc_plane_co_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_co_ns));
            hkn_enc_plane_cg_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_cg_ns));
            hkn_dec_header_samples_ms.push_back(ns_to_ms(dec_stats.decode_header_dir_ns));
            hkn_dec_plane_total_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_total_ns));
            hkn_dec_ycocg_to_rgb_samples_ms.push_back(ns_to_ms(dec_stats.decode_ycocg_to_rgb_ns));
            hkn_dec_plane_dispatch_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_dispatch_ns));
            hkn_dec_plane_wait_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_wait_ns));
            hkn_dec_ycocg_dispatch_samples_ms.push_back(ns_to_ms(dec_stats.decode_ycocg_dispatch_ns));
            hkn_dec_ycocg_kernel_samples_ms.push_back(ns_to_ms(dec_stats.decode_ycocg_kernel_ns));
            hkn_dec_ycocg_wait_samples_ms.push_back(ns_to_ms(dec_stats.decode_ycocg_wait_ns));
            hkn_dec_plane_try_natural_samples_ms.push_back(ns_to_ms(dec_stats.plane_try_natural_ns));
            hkn_dec_plane_screen_wrapper_samples_ms.push_back(ns_to_ms(dec_stats.plane_screen_wrapper_ns));
            hkn_dec_plane_block_types_samples_ms.push_back(ns_to_ms(dec_stats.plane_block_types_ns));
            hkn_dec_plane_filter_ids_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_ids_ns));
            hkn_dec_plane_filter_lo_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_lo_ns));
            hkn_dec_plane_filter_hi_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_hi_ns));
            hkn_dec_plane_reconstruct_samples_ms.push_back(ns_to_ms(dec_stats.plane_reconstruct_ns));
            hkn_dec_plane_y_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_y_ns));
            hkn_dec_plane_co_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_co_ns));
            hkn_dec_plane_cg_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_cg_ns));
            selected_samples.push_back(enc_stats.natural_row_selected_count);
            candidate_samples.push_back(enc_stats.natural_row_candidate_count);
            gain_samples.push_back(enc_stats.natural_row_gain_bytes_sum);
            loss_samples.push_back(enc_stats.natural_row_loss_bytes_sum);
            enc_parallel_3way_samples.push_back(enc_stats.perf_encode_plane_parallel_3way_count);
            enc_parallel_2way_samples.push_back(enc_stats.perf_encode_plane_parallel_2way_count);
            enc_parallel_seq_samples.push_back(enc_stats.perf_encode_plane_parallel_seq_count);
            enc_parallel_tokens_sum_samples.push_back(enc_stats.perf_encode_plane_parallel_tokens_sum);
            dec_parallel_3way_samples.push_back(dec_stats.decode_plane_parallel_3way_count);
            dec_parallel_seq_samples.push_back(dec_stats.decode_plane_parallel_seq_count);
            dec_parallel_tokens_sum_samples.push_back(dec_stats.decode_plane_parallel_tokens_sum);
            dec_ycocg_parallel_samples.push_back(dec_stats.decode_ycocg_parallel_count);
            dec_ycocg_seq_samples.push_back(dec_stats.decode_ycocg_sequential_count);
            dec_ycocg_threads_sum_samples.push_back(dec_stats.decode_ycocg_parallel_threads_sum);
            dec_ycocg_rows_sum_samples.push_back(dec_stats.decode_ycocg_rows_sum);
            dec_ycocg_pixels_sum_samples.push_back(dec_stats.decode_ycocg_pixels_sum);
            dec_filter_lo_mode_raw_samples.push_back(dec_stats.plane_filter_lo_mode_raw_count);
            dec_filter_lo_mode1_samples.push_back(dec_stats.plane_filter_lo_mode1_count);
            dec_filter_lo_mode2_samples.push_back(dec_stats.plane_filter_lo_mode2_count);
            dec_filter_lo_mode3_samples.push_back(dec_stats.plane_filter_lo_mode3_count);
            dec_filter_lo_mode4_samples.push_back(dec_stats.plane_filter_lo_mode4_count);
            dec_filter_lo_mode5_samples.push_back(dec_stats.plane_filter_lo_mode5_count);
            dec_filter_lo_mode_invalid_samples.push_back(dec_stats.plane_filter_lo_mode_invalid_count);
            dec_filter_lo_fallback_zero_fill_samples.push_back(dec_stats.plane_filter_lo_fallback_zero_fill_count);
            dec_filter_lo_mode4_parallel_tiles_samples.push_back(dec_stats.plane_filter_lo_mode4_parallel_ctx_tiles);
            dec_filter_lo_mode4_seq_tiles_samples.push_back(dec_stats.plane_filter_lo_mode4_sequential_ctx_tiles);
            dec_filter_lo_decode_rans_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_lo_decode_rans_ns));
            dec_filter_lo_decode_shared_rans_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_lo_decode_shared_rans_ns));
            dec_filter_lo_tilelz_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_lo_tilelz_decompress_ns));
            dec_recon_copy_fast_rows_samples.push_back(dec_stats.plane_recon_copy_fast_rows);
            dec_recon_copy_slow_rows_samples.push_back(dec_stats.plane_recon_copy_slow_rows);
            dec_recon_tile4_fast_quads_samples.push_back(dec_stats.plane_recon_tile4_fast_quads);
            dec_recon_tile4_slow_quads_samples.push_back(dec_stats.plane_recon_tile4_slow_quads);
            dec_recon_residual_missing_samples.push_back(dec_stats.plane_recon_residual_missing);
        }
    }
