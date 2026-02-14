    // Filter stream diagnostics (Phase 9n)
    {
        uint64_t fid_total = s.filter_ids_mode0 + s.filter_ids_mode1 + s.filter_ids_mode2;
        if (fid_total > 0 || s.filter_hi_sparse_count + s.filter_hi_dense_count > 0) {
            std::cout << "\n  Filter stream diagnostics\n";
            std::cout << "  filter_ids_mode0/1/2   "
                      << s.filter_ids_mode0 << "/"
                      << s.filter_ids_mode1 << "/"
                      << s.filter_ids_mode2 << "\n";
            if (s.filter_ids_raw_bytes_sum > 0) {
                double savings = 100.0 * (1.0 - (double)s.filter_ids_compressed_bytes_sum / (double)s.filter_ids_raw_bytes_sum);
                std::cout << "  filter_ids_bytes       raw=" << s.filter_ids_raw_bytes_sum
                          << " compressed=" << s.filter_ids_compressed_bytes_sum
                          << " (" << std::fixed << std::setprecision(1) << savings << "% savings)\n";
            }
            uint64_t fhi_total = s.filter_hi_sparse_count + s.filter_hi_dense_count;
            std::cout << "  filter_hi_sparse/dense " << s.filter_hi_sparse_count << "/" << s.filter_hi_dense_count << "\n";
            if (fhi_total > 0) {
                std::cout << "  filter_hi_avg_zero_ratio " << std::fixed << std::setprecision(1)
                          << (double)s.filter_hi_zero_ratio_sum / (double)fhi_total << "%\n";
            }
            if (s.filter_hi_raw_bytes_sum > 0) {
                double savings = 100.0 * (1.0 - (double)s.filter_hi_compressed_bytes_sum / (double)s.filter_hi_raw_bytes_sum);
                std::cout << "  filter_hi_bytes        raw=" << s.filter_hi_raw_bytes_sum
                          << " compressed=" << s.filter_hi_compressed_bytes_sum
                          << " (" << std::fixed << std::setprecision(1) << savings << "% savings)\n";
            }
        }
    }

    // Filter lo diagnostics (Phase 9o / 9p / 9q / 9u-next / 9x)
    {
        uint64_t flo_total = s.filter_lo_mode0 + s.filter_lo_mode1 + s.filter_lo_mode2 +
                             s.filter_lo_mode3 + s.filter_lo_mode4 + s.filter_lo_mode5 +
                             s.filter_lo_mode6 + s.filter_lo_mode7 + s.filter_lo_mode8;
        if (flo_total > 0) {
            std::cout << "\n  Filter lo diagnostics\n";
            std::cout << "  filter_lo_mode0/1/2/3/4/5/6/7/8 "
                      << s.filter_lo_mode0 << "/"
                      << s.filter_lo_mode1 << "/"
                      << s.filter_lo_mode2 << "/"
                      << s.filter_lo_mode3 << "/"
                      << s.filter_lo_mode4 << "/"
                      << s.filter_lo_mode5 << "/"
                      << s.filter_lo_mode6 << "/"
                      << s.filter_lo_mode7 << "/"
                      << s.filter_lo_mode8 << "\n";
            if (s.filter_lo_raw_bytes_sum > 0) {
                double savings = 100.0 * (1.0 - (double)s.filter_lo_compressed_bytes_sum / (double)s.filter_lo_raw_bytes_sum);
                std::cout << "  filter_lo_bytes        raw=" << s.filter_lo_raw_bytes_sum
                          << " compressed=" << s.filter_lo_compressed_bytes_sum
                          << " (" << std::fixed << std::setprecision(1) << savings << "% savings)\n";
            }
            if (s.filter_lo_mode3 > 0) {
                std::cout << "  mode3_rows_total       " << s.filter_lo_mode3_rows_sum << "\n";
                if (s.filter_lo_mode3_saved_bytes_sum > 0)
                     std::cout << "  mode3_saved_bytes      " << s.filter_lo_mode3_saved_bytes_sum << "\n";
                
                uint64_t ph_total = s.filter_lo_mode3_pred_hist[0] + s.filter_lo_mode3_pred_hist[1] + 
                                    s.filter_lo_mode3_pred_hist[2] + s.filter_lo_mode3_pred_hist[3];
                if (ph_total > 0) {
                    std::cout << "  mode3_pred_hist        NONE:" 
                              << (100.0 * s.filter_lo_mode3_pred_hist[0] / ph_total) << "% SUB:"
                              << (100.0 * s.filter_lo_mode3_pred_hist[1] / ph_total) << "% UP:"
                              << (100.0 * s.filter_lo_mode3_pred_hist[2] / ph_total) << "% AVG:"
                              << (100.0 * s.filter_lo_mode3_pred_hist[3] / ph_total) << "%\n";
                }
            }
            if (s.filter_lo_mode4 > 0) {
                if (s.filter_lo_mode4_saved_bytes_sum > 0) {
                    std::cout << "  mode4_saved_bytes      " << s.filter_lo_mode4_saved_bytes_sum << "\n";
                }
                std::cout << "  mode4_nonempty_tiles   " << s.filter_lo_ctx_nonempty_tiles << "\n";
                std::cout << "  mode4_ctx_raw_bytes    "
                          << s.filter_lo_ctx_bytes_sum[0] << "/"
                          << s.filter_lo_ctx_bytes_sum[1] << "/"
                          << s.filter_lo_ctx_bytes_sum[2] << "/"
                          << s.filter_lo_ctx_bytes_sum[3] << "/"
                          << s.filter_lo_ctx_bytes_sum[4] << "/"
                          << s.filter_lo_ctx_bytes_sum[5] << "\n";
            }
            if (s.filter_lo_mode5 > 0 && s.filter_lo_mode5_saved_bytes_sum > 0) {
                std::cout << "  mode5_saved_bytes      " << s.filter_lo_mode5_saved_bytes_sum << "\n";
            }
            if (s.filter_lo_mode5_candidates > 0) {
                std::cout << "  mode5_candidates       " << s.filter_lo_mode5_candidates << "\n";
                std::cout << "    rej_gate/rej_best    " << s.filter_lo_mode5_reject_gate << "/" << s.filter_lo_mode5_reject_best << "\n";
                double avg_cand = (double)s.filter_lo_mode5_candidate_bytes_sum / (double)s.filter_lo_mode5_candidates;
                double avg_wrap = (double)s.filter_lo_mode5_wrapped_bytes_sum / (double)s.filter_lo_mode5_candidates;
                double avg_leg  = (double)s.filter_lo_mode5_legacy_bytes_sum / (double)s.filter_lo_mode5_candidates;
                std::cout << "    avg_bytes(LZ/Wrap/Leg) " << std::fixed << std::setprecision(1)
                          << avg_cand << " / " << avg_wrap << " / " << avg_leg << "\n";
            }
            if (s.filter_lo_mode6 > 0 && s.filter_lo_mode6_saved_bytes_sum > 0) {
                std::cout << "  mode6_saved_bytes      " << s.filter_lo_mode6_saved_bytes_sum << "\n";
            }
            if (s.filter_lo_mode6_candidates > 0) {
                std::cout << "  mode6_candidates       " << s.filter_lo_mode6_candidates << "\n";
                std::cout << "    rej_gate/rej_best    " << s.filter_lo_mode6_reject_gate << "/" << s.filter_lo_mode6_reject_best << "\n";
                double avg_cand = (double)s.filter_lo_mode6_candidate_bytes_sum / (double)s.filter_lo_mode6_candidates;
                double avg_wrap = (double)s.filter_lo_mode6_wrapped_bytes_sum / (double)s.filter_lo_mode6_candidates;
                double avg_leg  = (double)s.filter_lo_mode6_legacy_bytes_sum / (double)s.filter_lo_mode6_candidates;
                std::cout << "    avg_bytes(LZ/Wrap/Leg) " << std::fixed << std::setprecision(1)
                          << avg_cand << " / " << avg_wrap << " / " << avg_leg << "\n";
                // Mode6 compact dist telemetry (Phase 9X)
                if (s.filter_lo_mode6_token_count_sum > 0) {
                    std::cout << "    tokens(match+lit)    " << s.filter_lo_mode6_token_count_sum
                              << " (" << s.filter_lo_mode6_match_tokens_sum << "+" << s.filter_lo_mode6_lit_tokens_sum << ")\n";
                    double avg_match = (double)s.filter_lo_mode6_match_count_sum / (double)s.filter_lo_mode6_candidates;
                    std::cout << "    avg_match_tokens       " << std::fixed << std::setprecision(1) << avg_match << "\n";
                    if (s.filter_lo_mode6_dist_saved_bytes_sum > 0) {
                        std::cout << "    dist_saved_bytes     " << s.filter_lo_mode6_dist_saved_bytes_sum << "\n";
                    }
                }
                // Mode6 v0x0017 telemetry (Phase 9X-2)
                if (s.filter_lo_mode6_v17_selected > 0) {
                    std::cout << "    v17_selected         " << s.filter_lo_mode6_v17_selected << "\n";
                    if (s.filter_lo_mode6_typebits_raw_bytes_sum > 0) {
                        double typebits_ratio = (double)s.filter_lo_mode6_typebits_enc_bytes_sum /
                                                (double)s.filter_lo_mode6_typebits_raw_bytes_sum;
                        std::cout << "    typebits raw/enc     " << s.filter_lo_mode6_typebits_raw_bytes_sum
                                  << "/" << s.filter_lo_mode6_typebits_enc_bytes_sum
                                  << " (ratio=" << std::fixed << std::setprecision(3) << typebits_ratio << ")\n";
                    }
                    if (s.filter_lo_mode6_lit_len_bytes_sum > 0 || s.filter_lo_mode6_match_len_bytes_sum > 0) {
                        std::cout << "    lit_len/match_len    " << s.filter_lo_mode6_lit_len_bytes_sum
                                  << "/" << s.filter_lo_mode6_match_len_bytes_sum << "\n";
                    }
                }
                if (s.filter_lo_mode6_fallback_to_mode0 > 0 || s.filter_lo_mode6_fallback_to_mode5 > 0) {
                    std::cout << "    fallback_to_mode0/5  " << s.filter_lo_mode6_fallback_to_mode0
                              << "/" << s.filter_lo_mode6_fallback_to_mode5 << "\n";
                }
                if (s.filter_lo_mode6_malformed_input > 0) {
                    std::cout << "    malformed_input      " << s.filter_lo_mode6_malformed_input << "\n";
                }
            }
            if (s.filter_lo_mode7 > 0 && s.filter_lo_mode7_saved_bytes_sum > 0) {
                std::cout << "  mode7_saved_bytes      " << s.filter_lo_mode7_saved_bytes_sum << "\n";
            }
            if (s.filter_lo_mode7_candidates > 0) {
                std::cout << "  mode7_candidates       " << s.filter_lo_mode7_candidates << "\n";
                std::cout << "    rej_gate/rej_best    " << s.filter_lo_mode7_reject_gate
                          << "/" << s.filter_lo_mode7_reject_best << "\n";
                double avg_wrap = (double)s.filter_lo_mode7_wrapped_bytes_sum /
                                  (double)s.filter_lo_mode7_candidates;
                double avg_leg  = (double)s.filter_lo_mode7_legacy_bytes_sum /
                                  (double)s.filter_lo_mode7_candidates;
                double avg_ctx  = (double)s.filter_lo_mode7_shared_ctx_sum /
                                  (double)s.filter_lo_mode7_candidates;
                std::cout << "    avg_bytes(Wrap/Leg)  " << std::fixed << std::setprecision(1)
                          << avg_wrap << " / " << avg_leg << "\n";
                std::cout << "    avg_shared_ctx       " << std::fixed << std::setprecision(2)
                          << avg_ctx << "\n";
            }
            if (s.filter_lo_mode8 > 0 && s.filter_lo_mode8_saved_bytes_sum > 0) {
                std::cout << "  mode8_saved_bytes      " << s.filter_lo_mode8_saved_bytes_sum << "\n";
            }
            if (s.filter_lo_mode8_candidates > 0) {
                std::cout << "  mode8_candidates       " << s.filter_lo_mode8_candidates << "\n";
                std::cout << "    rej_gate/rej_best    " << s.filter_lo_mode8_reject_gate
                          << "/" << s.filter_lo_mode8_reject_best << "\n";
                std::cout << "    ctx_sel(L/D/LZ)      " << s.filter_lo_mode8_ctx_legacy_sum << "/"
                          << s.filter_lo_mode8_ctx_delta_sum << "/"
                          << s.filter_lo_mode8_ctx_lz_sum << "\n";
                double avg_wrap = (double)s.filter_lo_mode8_wrapped_bytes_sum /
                                  (double)s.filter_lo_mode8_candidates;
                std::cout << "    avg_wrapped_bytes    " << std::fixed << std::setprecision(1)
                          << avg_wrap << "\n";
            }
            if (s.filter_lo_mode2_reject_gate > 0 || s.filter_lo_mode4_reject_gate > 0) {
                std::cout << "  rej_gate(mode2/4)      " << s.filter_lo_mode2_reject_gate << "/" << s.filter_lo_mode4_reject_gate << "\n";
            }
            if (s.filter_lo_lz_probe_enabled > 0) {
                std::cout << "  lz_probe enabled/chk/pass/skip "
                          << s.filter_lo_lz_probe_enabled << "/"
                          << s.filter_lo_lz_probe_checked << "/"
                          << s.filter_lo_lz_probe_pass << "/"
                          << s.filter_lo_lz_probe_skip << "\n";
                if (s.filter_lo_lz_probe_sample_bytes_sum > 0) {
                    double ratio = (double)s.filter_lo_lz_probe_sample_wrapped_bytes_sum /
                                   (double)s.filter_lo_lz_probe_sample_bytes_sum;
                    std::cout << "    probe_sample raw/lz/wrapped "
                              << s.filter_lo_lz_probe_sample_bytes_sum << "/"
                              << s.filter_lo_lz_probe_sample_lz_bytes_sum << "/"
                              << s.filter_lo_lz_probe_sample_wrapped_bytes_sum
                              << " (wrapped/raw=" << std::fixed << std::setprecision(3)
                              << ratio << ")\n";
                }
            }
        }

    }

    // Phase 9s-3: Screen-indexed gating telemetry
    {
        uint64_t sc_total = s.screen_candidate_count;
        if (sc_total > 0) {
            std::cout << "\n  Screen-indexed gating diagnostics\n";
            std::cout << "  screen_candidates      " << sc_total << "\n";
            std::cout << "  screen_selected        " << s.screen_selected_count << "\n";
            
            auto print_reject = [](const std::string& key, uint64_t val, uint64_t total) {
                double pct = (total > 0) ? (100.0 * (double)val / (double)total) : 0.0;
                std::cout << "    " << std::left << std::setw(21) << key
                          << std::right << std::setw(8) << val
                          << std::setw(9) << std::fixed << std::setprecision(2) << pct << "%\n";
            };
            uint64_t rejected_total = s.screen_rejected_pre_gate + s.screen_rejected_cost_gate;
            std::cout << "  screen_rejected        " << rejected_total << "\n";
            print_reject("pre_gate", s.screen_rejected_pre_gate, rejected_total);
            print_reject("cost_gate", s.screen_rejected_cost_gate, rejected_total);
            if (s.screen_rejected_pre_gate > 0) {
                std::cout << "    pre_small_tile       " << s.screen_rejected_small_tile << "\n";
                std::cout << "    pre_texture_gate     " << s.screen_rejected_prefilter_texture << "\n";
                std::cout << "    pre_build_fail       " << s.screen_rejected_build_fail << "\n";
                if (s.screen_rejected_build_fail > 0) {
                    std::cout << "      fail_too_many_unique " << s.screen_build_fail_too_many_unique << "\n";
                    std::cout << "      fail_empty_hist      " << s.screen_build_fail_empty_hist << "\n";
                    std::cout << "      fail_index_miss      " << s.screen_build_fail_index_miss << "\n";
                    std::cout << "      fail_other           " << s.screen_build_fail_other << "\n";
                }
                std::cout << "    pre_palette_limit    " << s.screen_rejected_palette_limit << "\n";
                std::cout << "    pre_bits_limit       " << s.screen_rejected_bits_limit << "\n";
            }
            
            if (s.screen_mode0_reject_count > 0) {
                std::cout << "    (mode0_too_big)      " << s.screen_mode0_reject_count << "\n";
            }
            
            uint64_t gate_total = s.screen_ui_like_count + s.screen_anime_like_count;
            if (gate_total > 0) {
                 std::cout << "  screen_profile_types   UI:" << s.screen_ui_like_count 
                           << " Anime:" << s.screen_anime_like_count << "\n";
            }

            std::cout << "\n";
            auto print_gain = [](const std::string& key, uint64_t val) {
                 std::cout << "  " << std::left << std::setw(23) << key << val << "\n";
            };

            if (s.screen_selected_count > 0) {
                 double avg_pal = (double)s.screen_palette_count_sum / (double)sc_total;
                 std::cout << "  avg_palette_count      " << std::fixed << std::setprecision(1) << avg_pal << "\n";
                 print_gain("total_gain_bytes", s.screen_gain_bytes_sum);
            }
            if (s.screen_compete_legacy_bytes_sum > 0) {
                 double ratio = (double)s.screen_compete_screen_bytes_sum /
                                (double)s.screen_compete_legacy_bytes_sum;
                 std::cout << "  compete_ratio(screen/legacy) " << std::fixed << std::setprecision(4)
                           << ratio << "\n";
            }
            if (s.screen_prefilter_eval_count > 0) {
                double avg_unique = (double)s.screen_prefilter_unique_sum /
                                    (double)s.screen_prefilter_eval_count;
                double avg_run = ((double)s.screen_prefilter_avg_run_x100_sum /
                                  (double)s.screen_prefilter_eval_count) / 100.0;
                double avg_mad = ((double)s.natural_prefilter_mad_x100_sum /
                                  (double)s.screen_prefilter_eval_count) / 100.0;
                double avg_entropy = ((double)s.natural_prefilter_entropy_x100_sum /
                                      (double)s.screen_prefilter_eval_count) / 100.0;
                std::cout << "  prefilter_avg(unique/run/mad/entropy) "
                          << std::fixed << std::setprecision(1)
                          << avg_unique << " / " << avg_run << " / "
                          << avg_mad << " / " << avg_entropy << "\n";
            }
            if (s.screen_rejected_cost_gate > 0) {
                 print_gain("total_avoided_bloat", s.screen_loss_bytes_sum);
            }
        }

