        if (s.natural_row_candidate_count > 0) {
            std::cout << "\n  Natural-row route diagnostics\n";
            std::cout << "  natural_candidates     " << s.natural_row_candidate_count << "\n";
            std::cout << "  natural_selected       " << s.natural_row_selected_count << "\n";
            std::cout << "  natural_rej_cost       " << s.natural_row_rejected_cost_gate << "\n";
            std::cout << "  natural_build_fail     " << s.natural_row_build_fail_count << "\n";
            if (s.natural_row_gain_bytes_sum > 0) {
                std::cout << "  natural_gain_bytes     " << s.natural_row_gain_bytes_sum << "\n";
            }
            if (s.natural_row_loss_bytes_sum > 0) {
                std::cout << "  natural_loss_bytes     " << s.natural_row_loss_bytes_sum << "\n";
            }
            std::cout << "  natural_row_mode0/1/2/3 " 
                      << s.natural_row_mode0_selected_count << "/"
                      << s.natural_row_mode1_selected_count << "/"
                      << s.natural_row_mode2_selected_count << "/"
                      << s.natural_row_mode3_selected_count << "\n";
            if (s.natural_prefilter_eval_count > 0) {
                double avg_unique = (double)s.natural_prefilter_unique_sum /
                                    (double)s.natural_prefilter_eval_count;
                double avg_run = ((double)s.natural_prefilter_avg_run_x100_sum /
                                  (double)s.natural_prefilter_eval_count) / 100.0;
                double avg_mad = ((double)s.natural_prefilter_mad_x100_sum /
                                  (double)s.natural_prefilter_eval_count) / 100.0;
                double avg_entropy = ((double)s.natural_prefilter_entropy_x100_sum /
                                      (double)s.natural_prefilter_eval_count) / 100.0;
                std::cout << "  natural_prefilter      pass/rej="
                          << s.natural_prefilter_pass_count << "/"
                          << s.natural_prefilter_reject_count << "\n";
                std::cout << "  natural_prefilter_avg  unique/run/mad/entropy="
                          << std::fixed << std::setprecision(1)
                          << avg_unique << " / " << avg_run << " / "
                          << avg_mad << " / " << avg_entropy << "\n";
            }
        }
        if (s.route_compete_policy_skip_count > 0) {
            std::cout << "\n  Route policy diagnostics\n";
            std::cout << "  route_policy_skipped   " << s.route_compete_policy_skip_count << "\n";
        }
        {
            const uint64_t total_parallel_paths =
                s.perf_encode_plane_parallel_3way_count +
                s.perf_encode_plane_parallel_2way_count +
                s.perf_encode_plane_parallel_seq_count;
            if (total_parallel_paths > 0) {
                std::cout << "\n  Encode parallel diagnostics\n";
                std::cout << "  plane_parallel_3way    " << s.perf_encode_plane_parallel_3way_count << "\n";
                std::cout << "  plane_parallel_2way    " << s.perf_encode_plane_parallel_2way_count << "\n";
                std::cout << "  plane_parallel_seq     " << s.perf_encode_plane_parallel_seq_count << "\n";
                std::cout << "  plane_parallel_tokens  " << s.perf_encode_plane_parallel_tokens_sum << "\n";
            }
        }

        std::cout << "\n  Lossless profile diagnostics\n";
        std::cout << "  profile_ui_tiles       " << s.profile_ui_tiles << "\n";
        std::cout << "  profile_anime_tiles    " << s.profile_anime_tiles << "\n";
        std::cout << "  profile_photo_tiles    " << s.profile_photo_tiles << "\n";

        uint64_t class_evals = s.class_eval_count;
        if (class_evals == 0) {
            class_evals = s.profile_ui_tiles + s.profile_anime_tiles + s.profile_photo_tiles;
        }
        if (class_evals > 0) {
            std::cout << "\n  Classifier diagnostics\n";
            std::cout << "    classifier_evals       " << class_evals << "\n";
            std::cout << "    avg_copy_hit_rate      " << std::fixed << std::setprecision(3)
                      << (double)s.class_copy_hit_x1000_sum / (double)(class_evals * 1000.0) << "\n";
            std::cout << "    avg_mean_abs_diff      " << std::fixed << std::setprecision(1)
                      << (double)s.class_mean_abs_diff_x1000_sum / (double)(class_evals * 1000.0) << "\n";
            std::cout << "    avg_active_bins        " << std::fixed << std::setprecision(1)
                      << (double)s.class_active_bins_sum / (double)class_evals << "\n";
            std::cout << "    anime_palette_bonus    " << s.anime_palette_bonus_applied << "\n";
        }

        if (s.palette_reorder_trials > 0) {
            std::cout << "\n  Palette Reorder diagnostics\n";
            std::cout << "  reorder_trials         " << s.palette_reorder_trials << "\n";
            std::cout << "  reorder_adopted        " << s.palette_reorder_adopted << "\n";
        }
    }
