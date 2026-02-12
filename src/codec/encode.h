#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/pindex.h"
#include "palette.h"
#include "copy.h"
#include "lossless_filter.h"
#include "band_groups.h"
#include "lz_tile.h"
#include "shared_cdf.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <limits>

namespace hakonyans {

/**
 * Lossless tile data layout:
 *   [4 bytes] filter_ids_size
 *   [4 bytes] lo_stream_size    (rANS-encoded low bytes)
 *   [4 bytes] hi_stream_size    (rANS-encoded high bytes)
 *   [4 bytes] pindex_size
 *   [filter_ids_size bytes] filter IDs (1 per row, per plane)
 *   [lo_stream_size bytes]  rANS stream for low bytes
 *   [hi_stream_size bytes]  rANS stream for high bytes
 *   [pindex_size bytes]     P-Index data (optional)
 */

class GrayscaleEncoder {
public:
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

        // Phase 9t-1: filter-block diagnostics
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

        // Candidate existed but another mode won.
        uint64_t tile4_rejected_by_copy;
        uint64_t tile4_rejected_by_palette;
        uint64_t tile4_rejected_by_filter;
        uint64_t copy_rejected_by_tile4;
        uint64_t copy_rejected_by_palette;
        uint64_t copy_rejected_by_filter;
        uint64_t palette_rejected_by_tile4;
        uint64_t palette_rejected_by_copy;
        uint64_t palette_rejected_by_filter;

        uint64_t est_copy_bits_sum;      // candidate blocks only
        uint64_t est_tile4_bits_sum;     // candidate blocks only
        uint64_t est_palette_bits_sum;   // candidate blocks only
        uint64_t est_filter_bits_sum;    // all blocks
        uint64_t est_selected_bits_sum;  // chosen mode only
        uint64_t est_copy_loss_bits_sum;    // copy candidate blocks where copy lost
        uint64_t est_tile4_loss_bits_sum;   // tile4 candidate blocks where tile4 lost
        uint64_t est_palette_loss_bits_sum; // palette candidate blocks where palette lost

        // Encoded stream diagnostics (actual bytes/bits).
        uint64_t block_types_bytes_sum;
        uint64_t block_type_runs_sum;
        uint64_t block_type_short_runs; // run length <= 2
        uint64_t block_type_long_runs;  // run length >= 16
        uint64_t block_type_runs_dct;
        uint64_t block_type_runs_palette;
        uint64_t block_type_runs_copy;
        uint64_t block_type_runs_tile4;
        uint64_t block_types_lz_used_count;      // New
        uint64_t block_types_lz_saved_bytes_sum; // New

        uint64_t palette_lz_used_count;          // New
        uint64_t palette_lz_saved_bytes_sum;     // New
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

        // Phase 9n: filter stream wrapper telemetry
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

        // Phase 9o: filter_lo wrapper telemetry
        uint64_t filter_lo_mode0;
        uint64_t filter_lo_mode1;
        uint64_t filter_lo_mode2;
        uint64_t filter_lo_raw_bytes_sum;
        uint64_t filter_lo_compressed_bytes_sum;

        // Phase 9p: filter_lo mode3
        uint64_t filter_lo_mode3;
        uint64_t filter_lo_mode3_rows_sum;
        uint64_t filter_lo_mode3_saved_bytes_sum;
        uint64_t filter_lo_mode3_pred_hist[4]; // 0:NONE, 1:SUB, 2:UP, 3:AVG
        uint64_t filter_lo_mode4;
        uint64_t filter_lo_mode4_saved_bytes_sum;
        uint64_t filter_lo_mode5;
        uint64_t filter_lo_mode5_saved_bytes_sum;
        uint64_t filter_lo_ctx_nonempty_tiles;
        uint64_t filter_lo_ctx_bytes_sum[6];

        // Phase 9u-tune: filter_lo mode selection diagnostics
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

        // Phase 9s-3: Screen-indexed gating telemetry
        uint64_t screen_candidate_count;
        uint64_t screen_selected_count;
        uint64_t screen_rejected_pre_gate;
        uint64_t screen_rejected_cost_gate;
        uint64_t screen_mode0_reject_count;
        uint64_t screen_ui_like_count;
        uint64_t screen_anime_like_count;
        uint64_t screen_palette_count_sum;
        uint64_t screen_bits_per_index_sum;
        uint64_t screen_gain_bytes_sum;
        uint64_t screen_loss_bytes_sum;

        // Phase 9s-4: Palette Reorder Telemetry
        uint64_t palette_reorder_trials;
        uint64_t palette_reorder_adopted;
        uint64_t palette_reorder_gain_bytes_sum; // Future use

        // Phase 9s-5: Profile Telemetry
        uint64_t profile_ui_tiles;
        uint64_t profile_anime_tiles;
        uint64_t profile_photo_tiles;

        // Phase 9s-6: Classifier diagnostics
        uint64_t class_eval_count;
        uint64_t class_copy_hit_x1000_sum;
        uint64_t class_mean_abs_diff_x1000_sum;
        uint64_t class_active_bins_sum;
        uint64_t anime_palette_bonus_applied;

        LosslessModeDebugStats() { reset(); }

        void reset() {
            total_blocks = 0;
            tile4_candidates = 0;
            copy_candidates = 0;
            palette_candidates = 0;
            copy_palette_overlap = 0;
            tile4_selected = 0;
            copy_selected = 0;
            palette_selected = 0;
            filter_selected = 0;
            filter_med_selected = 0;
            palette_rescue_attempted = 0;
            palette_rescue_adopted = 0;
            palette_rescue_gain_bits_sum = 0;
            filter_blocks_with_copy_candidate = 0;
            filter_blocks_with_palette_candidate = 0;
            filter_blocks_unique_le2 = 0;
            filter_blocks_unique_le4 = 0;
            filter_blocks_unique_le8 = 0;
            filter_blocks_unique_gt8 = 0;
            filter_blocks_transitions_sum = 0;
            filter_blocks_variance_proxy_sum = 0;
            filter_blocks_est_filter_bits_sum = 0;
            filter_diag_palette16_candidates = 0;
            filter_diag_palette16_better = 0;
            filter_diag_palette16_size_sum = 0;
            filter_diag_palette16_est_bits_sum = 0;
            filter_diag_palette16_gain_bits_sum = 0;
            filter_rows_with_pixels = 0;
            std::memset(filter_row_id_hist, 0, sizeof(filter_row_id_hist));
            tile4_rejected_by_copy = 0;
            tile4_rejected_by_palette = 0;
            tile4_rejected_by_filter = 0;
            copy_rejected_by_tile4 = 0;
            copy_rejected_by_palette = 0;
            copy_rejected_by_filter = 0;
            palette_rejected_by_tile4 = 0;
            palette_rejected_by_copy = 0;
            palette_rejected_by_filter = 0;
            est_copy_bits_sum = 0;
            est_tile4_bits_sum = 0;
            est_palette_bits_sum = 0;
            est_filter_bits_sum = 0;
            est_selected_bits_sum = 0;
            est_copy_loss_bits_sum = 0;
            est_tile4_loss_bits_sum = 0;
            est_palette_loss_bits_sum = 0;
            block_types_bytes_sum = 0;
            block_type_runs_sum = 0;
            block_type_short_runs = 0;
            block_type_long_runs = 0;
            block_type_runs_dct = 0;
            block_type_runs_palette = 0;
            block_type_runs_copy = 0;
            block_type_runs_tile4 = 0;
            block_types_lz_used_count = 0;
            block_types_lz_saved_bytes_sum = 0;
            palette_lz_used_count = 0;
            palette_lz_saved_bytes_sum = 0;
            palette_stream_bytes_sum = 0;
            palette_stream_raw_bytes_sum = 0;
            palette_stream_v2_count = 0;
            palette_stream_v3_count = 0;
            palette_stream_mask_dict_count = 0;
            palette_stream_mask_dict_entries = 0;
            palette_stream_palette_dict_count = 0;
            palette_stream_palette_dict_entries = 0;
            palette_blocks_parsed = 0;
            palette_blocks_prev_reuse = 0;
            palette_blocks_dict_ref = 0;
            palette_blocks_raw_colors = 0;
            palette_blocks_two_color = 0;
            palette_blocks_multi_color = 0;
            palette_parse_errors = 0;
            palette_stream_compact_count = 0;
            palette_stream_compact_saved_bytes_sum = 0;
            copy_stream_count = 0;
            copy_stream_mode0 = 0;
            copy_stream_mode1 = 0;
            copy_stream_mode2 = 0;
            copy_stream_mode3 = 0;
            copy_wrap_mode0 = 0;
            copy_wrap_mode1 = 0;
            copy_wrap_mode2 = 0;
            copy_mode3_run_tokens_sum = 0;
            copy_mode3_runs_sum = 0;
            copy_mode3_long_runs = 0;
            copy_lz_used_count = 0;
            copy_lz_saved_bytes_sum = 0;
            copy_stream_bytes_sum = 0;
            copy_stream_payload_bits_sum = 0;
            copy_stream_overhead_bits_sum = 0;
            copy_ops_total = 0;
            copy_ops_small = 0;
            copy_ops_raw = 0;
            copy_mode2_zero_bit_streams = 0;
            copy_mode2_dynamic_bits_sum = 0;
            tile4_stream_mode0 = 0;
            tile4_stream_mode1 = 0;
            tile4_stream_mode2 = 0;
            tile4_stream_raw_bytes_sum = 0;
            tile4_stream_bytes_sum = 0;
            filter_ids_mode0 = 0;
            filter_ids_mode1 = 0;
            filter_ids_mode2 = 0;
            filter_ids_raw_bytes_sum = 0;
            filter_ids_compressed_bytes_sum = 0;
            filter_hi_sparse_count = 0;
            filter_hi_dense_count = 0;
            filter_hi_zero_ratio_sum = 0;
            filter_hi_raw_bytes_sum = 0;
            filter_hi_compressed_bytes_sum = 0;
            filter_lo_mode0 = 0;
            filter_lo_mode1 = 0;
            filter_lo_mode2 = 0;
            filter_lo_mode3 = 0;
            filter_lo_mode3_rows_sum = 0;
            filter_lo_mode3_saved_bytes_sum = 0;
            std::memset(filter_lo_mode3_pred_hist, 0, sizeof(filter_lo_mode3_pred_hist));
            filter_lo_mode4 = 0;
            filter_lo_mode4_saved_bytes_sum = 0;
            filter_lo_mode5 = 0;
            filter_lo_mode5_saved_bytes_sum = 0;
            filter_lo_ctx_nonempty_tiles = 0;
            std::memset(filter_lo_ctx_bytes_sum, 0, sizeof(filter_lo_ctx_bytes_sum));
            filter_lo_raw_bytes_sum = 0;
            filter_lo_compressed_bytes_sum = 0;
            filter_lo_mode2_reject_gate = 0;
            filter_lo_mode4_reject_gate = 0;
            filter_lo_mode5_candidates = 0;
            filter_lo_mode5_reject_gate = 0;
            filter_lo_mode5_reject_best = 0;
            filter_lo_mode2_candidate_bytes_sum = 0;
            filter_lo_mode4_candidate_bytes_sum = 0;
            filter_lo_mode5_candidate_bytes_sum = 0;
            filter_lo_mode5_wrapped_bytes_sum = 0;
            filter_lo_mode5_legacy_bytes_sum = 0;
            screen_candidate_count = 0;
            screen_selected_count = 0;
            screen_rejected_pre_gate = 0;
            screen_rejected_cost_gate = 0;
            screen_mode0_reject_count = 0;
            screen_ui_like_count = 0;
            screen_anime_like_count = 0;
            screen_palette_count_sum = 0;
            screen_bits_per_index_sum = 0;
            screen_gain_bytes_sum = 0;
            screen_loss_bytes_sum = 0;
            palette_reorder_trials = 0;
            palette_reorder_adopted = 0;
            palette_reorder_gain_bytes_sum = 0;
            profile_ui_tiles = 0;
            profile_anime_tiles = 0;
            profile_photo_tiles = 0;
            class_eval_count = 0;
            class_copy_hit_x1000_sum = 0;
            class_mean_abs_diff_x1000_sum = 0;
            class_active_bins_sum = 0;
            anime_palette_bonus_applied = 0;
        }

    };

    static void reset_lossless_mode_debug_stats() {
        tl_lossless_mode_debug_stats_.reset();
    }

    static LosslessModeDebugStats get_lossless_mode_debug_stats() {
        return tl_lossless_mode_debug_stats_;
    }

private:
    inline static thread_local LosslessModeDebugStats tl_lossless_mode_debug_stats_;

public:
    enum class LosslessProfile : uint8_t { UI = 0, ANIME = 1, PHOTO = 2 };

public:
    // Heuristic profile for applying lossless mode biases.
    // Classify from sampled exact-copy hit rate, local gradient, and histogram density.
    static LosslessProfile classify_lossless_profile(const int16_t* y_plane, uint32_t width, uint32_t height) {
        if (!y_plane || width == 0 || height == 0) return LosslessProfile::PHOTO;

        const int bx = (int)((width + 7) / 8);
        const int by = (int)((height + 7) / 8);
        if (bx * by < 64) return LosslessProfile::PHOTO; // avoid unstable decisions on tiny images

        const CopyParams kCopyCandidates[4] = {
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
        };

        auto sample_at = [&](int x, int y) -> int16_t {
            int sx = std::clamp(x, 0, (int)width - 1);
            int sy = std::clamp(y, 0, (int)height - 1);
            return y_plane[(size_t)sy * width + (size_t)sx];
        };

        int step = 4; // sample every 4th block in X/Y
        int total_blocks = bx * by;
        if (total_blocks < 256) step = 1;
        else if (total_blocks < 1024) step = 2;

        int samples = 0;
        int copy_hits = 0;
        
        uint64_t sum_abs_diff = 0;
        uint64_t pixel_count = 0;
        uint32_t hist[16] = {0};

        for (int yb = 0; yb < by; yb += step) {
            for (int xb = 0; xb < bx; xb += step) {
                int cur_x = xb * 8;
                int cur_y = yb * 8;
                bool hit = false;

                // Copy Check
                for (const auto& cand : kCopyCandidates) {
                    int src_x = cur_x + cand.dx;
                    int src_y = cur_y + cand.dy;
                    if (src_x < 0 || src_y < 0) continue;
                    if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                    bool match = true;
                    for (int y = 0; y < 8 && match; y++) {
                        for (int x = 0; x < 8; x++) {
                            if (sample_at(cur_x + x, cur_y + y) != sample_at(src_x + x, src_y + y)) {
                                match = false;
                                break;
                            }
                        }
                    }
                    if (match) {
                        hit = true;
                        break;
                    }
                }

                if (hit) copy_hits++;
                
                // Stats (Gradient & Histogram)
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                         int16_t val = sample_at(cur_x + x, cur_y + y);
                         // Histogram (16 levels)
                         int bin = std::clamp((int)val, 0, 255) / 16;
                         if (bin >= 0 && bin < 16) hist[bin]++;
                         
                         // Gradient (adjacent diffs)
                         if (x > 0) sum_abs_diff += (uint64_t)std::abs(val - sample_at(cur_x + x - 1, cur_y + y));
                         if (y > 0) sum_abs_diff += (uint64_t)std::abs(val - sample_at(cur_x + x, cur_y + y - 1));
                    }
                }
                
                samples++;
                pixel_count += 64;
            }
        }

        if (samples < 32) return LosslessProfile::PHOTO;
        
        const double copy_hit_rate = (double)copy_hits / (double)samples;
        double mean_abs_diff = 0.0;
        if (pixel_count > 0) {
             // Normalized by pixel count. Note: sum_abs_diff counts ~2 diffs per pixel (less on boundaries).
             // We keep it simple: sum / pixels.
             mean_abs_diff = (double)sum_abs_diff / (double)pixel_count;
        }
        
        int active_bins = 0;
        for(int k=0; k<16; k++) if(hist[k] > 0) active_bins++;

        // Phase 9s-6: Telemetry
        tl_lossless_mode_debug_stats_.class_eval_count++;
        tl_lossless_mode_debug_stats_.class_copy_hit_x1000_sum += (uint64_t)(copy_hit_rate * 1000.0);
        tl_lossless_mode_debug_stats_.class_mean_abs_diff_x1000_sum += (uint64_t)(mean_abs_diff * 1000.0);
        tl_lossless_mode_debug_stats_.class_active_bins_sum += (uint64_t)active_bins;

        // Flat anime scenes (large painted areas with very low local gradients)
        // are easily mistaken as UI by coarse histogram-only rules.
        if (copy_hit_rate >= 0.10 && active_bins <= 6 && mean_abs_diff <= 1.2) {
            return LosslessProfile::ANIME;
        }

        int ui_score = 0;
        int anime_score = 0;

        // UI signals
        if (copy_hit_rate >= 0.90) ui_score += 3;
        if (active_bins <= 10)     ui_score += 2;
        if (mean_abs_diff <= 12)   ui_score += 1;

        // Anime signals
        if (copy_hit_rate >= 0.60 && copy_hit_rate < 0.95) anime_score += 2;
        if (active_bins >= 8 && active_bins <= 24)         anime_score += 2;
        if (mean_abs_diff <= 28)                           anime_score += 2;

        // Decision
        LosslessProfile result = LosslessProfile::PHOTO;
        if (ui_score >= anime_score + 2) result = LosslessProfile::UI;
        else if (anime_score >= 3)       result = LosslessProfile::ANIME;

        return result;
    }

    static uint32_t extract_tile_cfl_size(const std::vector<uint8_t>& tile_data, bool use_band_group_cdf) {
        if (use_band_group_cdf) {
            if (tile_data.size() < 40) return 0;
            uint32_t sz[10];
            std::memcpy(sz, tile_data.data(), 40);
            return sz[6];
        }
        if (tile_data.size() < 32) return 0;
        uint32_t sz[8];
        std::memcpy(sz, tile_data.data(), 32);
        return sz[4];
    }

    static std::vector<uint8_t> serialize_cfl_legacy(const std::vector<CfLParams>& cfl_params) {
        std::vector<uint8_t> out;
        if (cfl_params.empty()) return out;
        out.reserve(cfl_params.size() * 2);
        for (const auto& p : cfl_params) {
            int a_q6 = (int)std::lround(std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f));
            int b_center = (int)std::lround(std::clamp(p.beta_cb, 0.0f, 255.0f));
            // Legacy predictor: pred = a*y + b_legacy
            // Current centered model: pred = a*(y-128) + b_center
            int b_legacy = std::clamp(b_center - 2 * a_q6, 0, 255);
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(a_q6)));
            out.push_back(static_cast<uint8_t>(b_legacy));
        }
        return out;
    }

    static std::vector<uint8_t> serialize_cfl_adaptive(const std::vector<CfLParams>& cfl_params) {
        std::vector<uint8_t> out;
        if (cfl_params.empty()) return out;

        const int nb = (int)cfl_params.size();
        int applied_count = 0;
        for (const auto& p : cfl_params) {
            if (p.alpha_cr > 0.5f) applied_count++;
        }
        if (applied_count == 0) return out;

        const size_t mask_bytes = ((size_t)nb + 7) / 8;
        out.resize(mask_bytes, 0);
        out.reserve(mask_bytes + (size_t)applied_count * 2);
        for (int i = 0; i < nb; i++) {
            if (cfl_params[i].alpha_cr > 0.5f) {
                out[(size_t)i / 8] |= (uint8_t)(1u << (i % 8));
                int a_q6 = (int)std::lround(std::clamp(cfl_params[i].alpha_cb * 64.0f, -128.0f, 127.0f));
                int b = (int)std::lround(std::clamp(cfl_params[i].beta_cb, 0.0f, 255.0f));
                out.push_back(static_cast<uint8_t>(static_cast<int8_t>(a_q6)));
                out.push_back(static_cast<uint8_t>(b));
            }
        }
        return out;
    }

    static std::vector<uint8_t> build_cfl_payload(const std::vector<CfLParams>& cfl_params) {
        if (cfl_params.empty()) return {};
        bool any_applied = false;
        for (const auto& p : cfl_params) {
            if (p.alpha_cr > 0.5f) {
                any_applied = true;
                break;
            }
        }
        if (!any_applied) return {};

        auto adaptive = serialize_cfl_adaptive(cfl_params);
        const size_t legacy_size = cfl_params.size() * 2;
        // If sizes collide, keep legacy to avoid decode-side ambiguity.
        if (!adaptive.empty() && adaptive.size() != legacy_size) {
            return adaptive;
        }

        // Safe fallback for ambiguous sizes.
        return serialize_cfl_legacy(cfl_params);
    }

    static std::vector<uint8_t> encode(const uint8_t* pixels, uint32_t width, uint32_t height, uint8_t quality = 75) {
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 1; header.colorspace = 2; header.subsampling = 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        uint32_t pad_w = header.padded_width(), pad_h = header.padded_height();
        uint16_t quant[64]; QuantTable::build_quant_table(quality, quant);
        int target_pi_meta_ratio = (quality >= 90) ? 1 : 2;
        auto tile_data = encode_plane(
            pixels, width, height, pad_w, pad_h, quant,
            true, true, nullptr, 0, nullptr, nullptr, false, true,
            target_pi_meta_ratio
        );
        QMATChunk qmat; qmat.quality = quality; qmat.num_tables = 1; std::memcpy(qmat.quant_y, quant, 128);
        auto qmat_data = qmat.serialize();
        ChunkDirectory dir; dir.add("QMAT", 0, qmat_data.size()); dir.add("TIL0", 0, tile_data.size());
        auto dir_data = dir.serialize();
        size_t qmat_offset = 48 + dir_data.size();
        size_t tile_offset = qmat_offset + qmat_data.size();
        dir.entries[0].offset = qmat_offset; dir.entries[1].offset = tile_offset;
        dir_data = dir.serialize();
        std::vector<uint8_t> output; output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        return output;
    }

    static std::vector<uint8_t> encode_color(const uint8_t* rgb_data, uint32_t width, uint32_t height, uint8_t quality = 75, bool use_420 = true, bool use_cfl = true, bool enable_screen_profile = false) {
        std::vector<uint8_t> y_plane(width * height), cb_plane(width * height), cr_plane(width * height);
        for (uint32_t i = 0; i < width * height; i++) rgb_to_ycbcr(rgb_data[i*3], rgb_data[i*3+1], rgb_data[i*3+2], y_plane[i], cb_plane[i], cr_plane[i]);
        bool use_band_group_cdf = (quality <= 70);
        int target_pi_meta_ratio = (quality >= 90) ? 1 : 2;
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 3; header.colorspace = 0; header.subsampling = use_420 ? 1 : 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        if (!use_band_group_cdf) header.version = FileHeader::MIN_SUPPORTED_VERSION;  // v0.3 legacy AC stream
        uint16_t quant_y[64], quant_c[64];
        int chroma_quality = std::clamp((int)quality - 12, 1, 100);
        QuantTable::build_quant_tables(quality, chroma_quality, quant_y, quant_c);
        uint32_t pad_w_y = header.padded_width(), pad_h_y = header.padded_height();
        auto tile_y = encode_plane(
            y_plane.data(), width, height, pad_w_y, pad_h_y, quant_y,
            true, true, nullptr, 0, nullptr, nullptr,
            enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
        );
        std::vector<uint8_t> tile_cb, tile_cr;
        bool any_cfl_payload = false;
        auto encode_chroma_best = [&](const uint8_t* chroma_pixels, uint32_t cw, uint32_t ch, uint32_t cpw, uint32_t cph, const std::vector<uint8_t>* y_for_cfl, int cidx) {
            auto without_cfl = encode_plane(
                chroma_pixels, cw, ch, cpw, cph, quant_c,
                true, true, nullptr, cidx, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
            if (!use_cfl || y_for_cfl == nullptr) return without_cfl;

            auto with_cfl = encode_plane(
                chroma_pixels, cw, ch, cpw, cph, quant_c,
                true, true, y_for_cfl, cidx, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
            if (with_cfl.size() < without_cfl.size()) {
                any_cfl_payload |= (extract_tile_cfl_size(with_cfl, use_band_group_cdf) > 0);
                return with_cfl;
            }
            return without_cfl;
        };

        if (use_420) {
            int cb_w, cb_h; std::vector<uint8_t> cb_420, cr_420, y_ds;
            downsample_420(cb_plane.data(), width, height, cb_420, cb_w, cb_h);
            downsample_420(cr_plane.data(), width, height, cr_420, cb_w, cb_h);
            uint32_t pad_w_c = ((cb_w + 7) / 8) * 8, pad_h_c = ((cb_h + 7) / 8) * 8;
            if (use_cfl) { downsample_420(y_plane.data(), width, height, y_ds, cb_w, cb_h); }
            tile_cb = encode_chroma_best(cb_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, use_cfl ? &y_ds : nullptr, 0);
            tile_cr = encode_chroma_best(cr_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, use_cfl ? &y_ds : nullptr, 1);
        } else {
            tile_cb = encode_chroma_best(cb_plane.data(), width, height, pad_w_y, pad_h_y, use_cfl ? &y_plane : nullptr, 0);
            tile_cr = encode_chroma_best(cr_plane.data(), width, height, pad_w_y, pad_h_y, use_cfl ? &y_plane : nullptr, 1);
        }
        if (any_cfl_payload) header.flags |= 2;

        QMATChunk qmat;
        qmat.quality = quality;
        qmat.num_tables = 3;
        std::memcpy(qmat.quant_y, quant_y, 128);
        std::memcpy(qmat.quant_cb, quant_c, 128);
        std::memcpy(qmat.quant_cr, quant_c, 128);
        auto qmat_data = qmat.serialize();
        ChunkDirectory dir; dir.add("QMAT", 0, qmat_data.size()); dir.add("TIL0", 0, tile_y.size()); dir.add("TIL1", 0, tile_cb.size()); dir.add("TIL2", 0, tile_cr.size());
        auto dir_data = dir.serialize(); size_t offset = 48 + dir_data.size();
        for (int i = 0; i < 4; i++) { dir.entries[i].offset = offset; offset += (i==0?qmat_data.size():(i==1?tile_y.size():(i==2?tile_cb.size():tile_cr.size()))); }
        dir_data = dir.serialize();
        std::vector<uint8_t> output; output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end()); output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_y.begin(), tile_y.end()); output.insert(output.end(), tile_cb.begin(), tile_cb.end()); output.insert(output.end(), tile_cr.begin(), tile_cr.end());
        return output;
    }

public:
    static std::vector<uint8_t> encode_plane(
        const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h,
        const uint16_t quant[64], bool pi=false, bool aq=false, const std::vector<uint8_t>* y_ref=nullptr, int chroma_idx=0,
        const std::vector<FileHeader::BlockType>* block_types_in = nullptr,
        const std::vector<CopyParams>* copy_params_in = nullptr,
        bool enable_screen_profile = false,
        bool use_band_group_cdf = true,
        int target_pindex_meta_ratio_percent = 2
    ) {
        std::vector<uint8_t> padded = pad_image(pixels, width, height, pad_w, pad_h);
        std::vector<uint8_t> y_padded; if (y_ref) y_padded = pad_image(y_ref->data(), (y_ref->size() > width*height/2 ? width : (width+1)/2), (y_ref->size() > width*height/2 ? height : (height+1)/2), pad_w, pad_h);
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;
        
        // Block Types handling
        std::vector<FileHeader::BlockType> block_types;
        if (block_types_in && block_types_in->size() == nb) {
            block_types = *block_types_in;
        } else {
            block_types.assign(nb, FileHeader::BlockType::DCT);
        }
        
        
        // std::vector<uint8_t> bt_data = encode_block_types(block_types); // Moved to after loop


        std::vector<std::vector<int16_t>> dct_blocks(nb, std::vector<int16_t>(64));
        std::vector<float> activities(nb); float total_activity = 0.0f;
        std::vector<CfLParams> cfl_params;
        
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        
        std::vector<CopyParams> copy_ops;
        int copy_op_idx = 0;

        for (int i = 0; i < nb; i++) {
            int bx = i % nx, by = i / nx; int16_t block[64]; extract_block(padded.data(), pad_w, pad_h, bx, by, block);
            
            // Automatic Block Type Selection Logic
            // Priority: Forced Input -> Copy (Search) -> Palette (Check) -> DCT
            
            FileHeader::BlockType selected_type = FileHeader::BlockType::DCT;
            
            // 1. Check Forced Input
            if (block_types_in && i < (int)block_types_in->size()) {
                selected_type = (*block_types_in)[i];
            } else if (enable_screen_profile) {
                // Automatic selection only if Screen Profile is enabled
                // A. Try Copy
                // Limit search to mainly text/UI areas (high variance? or always try?)
                // Copy search is expensive. For V1, let's limit radius or try only if variance is high?
                // Actually, Palette/Copy are good for flat areas too.
                // Let's try Copy Search with small radius (e.g. 128 px)
                
                CopyParams cp;
                // Note: We should search in `padded` (original pixels)?
                // Ideally we search in `reconstructed` pixels for correctness (drift prevention).
                // But `dct_blocks` haven't been quantized/dequantized yet!
                // We are in the loop `for (int i=0; i<nb; i++)`.
                // Previous blocks `0..i-1` have been processed but their RECONSTRUCTED pixels are not stored yet?
                // `encode_plane` calculates `quantized` coeff in the NEXT loop.
                // So strict IntraBC is impossible in this single-pass structure without buffering.
                //
                // SOLUTION for Multi-Pass:
                // We need to reconstruct blocks as we go if we want to search in them.
                // OR: We assume "high quality" and search in Processed Original pixels.
                // Screen content is usually lossless-ish.
                // Let's search in `padded` (Original) for Step 4 V1.
                // This introduces slight mismatch (Encoder thinks match, Decoder sees reconstructed diff).
                // But for lossless-like settings (Q90+), it matches.
                
                int sad = IntraBCSearch::search(padded.data(), pad_w, pad_h, bx, by, 64, cp);
                if (sad == 0) { // Perfect match
                    selected_type = FileHeader::BlockType::COPY;
                    copy_ops.push_back(cp);
                    block_types[i] = selected_type;
                    continue; // Skip Palette/DCT
                }
                
                // B. Try Palette
                Palette p = PaletteExtractor::extract(block, 8);
                if (p.size > 0 && p.size <= 8) {
                    // Check error? PaletteExtractor as implemented is "Lossless if <= 8 colors"
                    // If it returned a palette, it means the block HAD <= 8 colors.
                    selected_type = FileHeader::BlockType::PALETTE;
                }
            }
            
            // Apply selection
            block_types[i] = selected_type;

            if (selected_type == FileHeader::BlockType::COPY) {
                 // Already handled above if auto-detected.
                 // But if forced via input, we need to push param.
                 if (copy_params_in && copy_op_idx < (int)copy_params_in->size()) {
                    copy_ops.push_back((*copy_params_in)[copy_op_idx++]);
                 } else {
                    // Fallback if forced but no param?
                    // Should rely on auto-search if forced but no param?
                    // For now assume forced input comes with params or we re-search?
                    // Let's re-search if param missing.
                    CopyParams cp;
                    IntraBCSearch::search(padded.data(), pad_w, pad_h, bx, by, 64, cp);
                    copy_ops.push_back(cp);
                 }
                 if (y_ref) cfl_params.push_back({0.0f, 128.0f, 0.0f, 0.0f});
                 continue;
            } else if (selected_type == FileHeader::BlockType::PALETTE) {
                // Try to extract palette (redundant if we just did it, but safe)
                Palette p = PaletteExtractor::extract(block, 8);
                if (p.size > 0) {
                    palettes.push_back(p);
                    palette_indices.push_back(PaletteExtractor::map_indices(block, p));
                    if (y_ref) cfl_params.push_back({0.0f, 128.0f, 0.0f, 0.0f});
                    continue; 
                } else {
                    // Start of fallback to DCT
                    block_types[i] = FileHeader::BlockType::DCT;
                }
            }
            
            // Fallthrough to DCT logic
            bool cfl_applied = false;
            int cfl_alpha_q8 = 0, cfl_beta = 128;

            if (y_ref) {
                int16_t yb[64]; extract_block(y_padded.data(), pad_w, pad_h, bx, by, yb);
                uint8_t yu[64], cu[64]; 
                int64_t mse_no_cfl = 0;
                for (int k=0; k<64; k++) { 
                    yu[k]=(uint8_t)(yb[k]+128); 
                    cu[k]=(uint8_t)(block[k]+128); 
                    int err = (int)cu[k] - 128;
                    mse_no_cfl += (int64_t)err * err;
                }
                
                int alpha_q8, beta;
                compute_cfl_block_adaptive(yu, cu, alpha_q8, beta);
                
                // Reconstruct exactly as the decoder would (centered model)
                int a_q6 = (int)std::lround(std::clamp((float)alpha_q8 / 256.0f * 64.0f, -128.0f, 127.0f));
                int b_center = (int)std::lround(std::clamp((float)beta, 0.0f, 255.0f));

                int64_t mse_cfl_recon = 0;
                for (int k=0; k<64; k++) {
                    // Decoder formula: p = (a6 * (py - 128) + 32) >> 6 + b
                    int p = (a_q6 * (yu[k] - 128) + 32) >> 6;
                    p += b_center;
                    int err = (int)cu[k] - std::clamp(p, 0, 255);
                    mse_cfl_recon += (int64_t)err * err;
                }

                // Adaptive Decision: MSE based on EXACT decoder predictor.
                if (mse_cfl_recon < mse_no_cfl - 1024) {
                    cfl_applied = true;
                    cfl_alpha_q8 = (a_q6 * 256) / 64; 
                    cfl_beta = b_center;
                    
                    for (int k=0; k<64; k++) {
                        int p = (a_q6 * (yu[k] - 128) + 32) >> 6;
                        p += b_center;
                        block[k] = (int16_t)std::clamp((int)cu[k] - p, -128, 127);
                    }
                }
            }
            
            if (y_ref) {
                cfl_params.push_back({(float)cfl_alpha_q8 / 256.0f, (float)cfl_beta, cfl_applied ? 1.0f : 0.0f, 0.0f});
            }

            int16_t dct_out[64], zigzag[64]; DCT::forward(block, dct_out); Zigzag::scan(dct_out, zigzag);
            std::memcpy(dct_blocks[i].data(), zigzag, 128);
            if (aq) { float act = QuantTable::calc_activity(&zigzag[1]); activities[i] = act; total_activity += act; }
        }
        float avg_activity = total_activity / nb;
        std::vector<Token> dc_tokens;
        std::vector<Token> ac_tokens;
        std::vector<Token> ac_low_tokens;
        std::vector<Token> ac_mid_tokens;
        std::vector<Token> ac_high_tokens;
        std::vector<int8_t> q_deltas; if (aq) q_deltas.reserve(nb);
        int16_t prev_dc = 0;
        for (int i = 0; i < nb; i++) {
            if (i < (int)block_types.size() && (block_types[i] == FileHeader::BlockType::PALETTE || block_types[i] == FileHeader::BlockType::COPY)) {
                // Skip DCT encoding for palette/copy blocks
                 continue;
            }

            float scale = 1.0f; if (aq) { scale = QuantTable::get_adaptive_scale(activities[i], avg_activity); int8_t delta = (int8_t)std::clamp((scale - 1.0f) * 50.0f, -127.0f, 127.0f); q_deltas.push_back(delta); scale = 1.0f + (delta / 50.0f); }
            int16_t quantized[64]; for (int k = 0; k < 64; k++) { int16_t coeff = dct_blocks[i][k]; uint16_t q_adj = std::max((uint16_t)1, (uint16_t)std::round(quant[k] * scale)); int sign = (coeff < 0) ? -1 : 1; quantized[k] = sign * ((std::abs(coeff) + q_adj / 2) / q_adj); }
            int16_t dc_diff = quantized[0] - prev_dc; prev_dc = quantized[0]; dc_tokens.push_back(Tokenizer::tokenize_dc(dc_diff));
            if (use_band_group_cdf) {
                tokenize_ac_band(quantized, BAND_LOW, ac_low_tokens);
                tokenize_ac_band(quantized, BAND_MID, ac_mid_tokens);
                tokenize_ac_band(quantized, BAND_HIGH, ac_high_tokens);
            } else {
                auto at = Tokenizer::tokenize_ac(&quantized[1]);
                ac_tokens.insert(ac_tokens.end(), at.begin(), at.end());
            }
        }
        
        std::vector<uint8_t> bt_data = encode_block_types(block_types);
        // Phase 9s-4: Modify this call to collect telemetry
        int reorder_trials = 0;
        int reorder_adopted = 0;
        std::vector<uint8_t> pal_data = PaletteCodec::encode_palette_stream(
             palettes, palette_indices, false,
             &reorder_trials, &reorder_adopted
        );
        tl_lossless_mode_debug_stats_.palette_reorder_trials += reorder_trials;
        tl_lossless_mode_debug_stats_.palette_reorder_adopted += reorder_adopted;

        // Phase 9l-3: Tile-local LZ for Palette Stream
        if (!pal_data.empty()) {
             std::vector<uint8_t> lz = TileLZ::compress(pal_data);
             size_t wrapped_size = 6 + lz.size();
             // Apply only if size reduction >= 2%
             if (wrapped_size * 100 <= pal_data.size() * 98) {
                 std::vector<uint8_t> wrapped;
                 wrapped.resize(6);
                 wrapped[0] = FileHeader::WRAPPER_MAGIC_PALETTE;
                 wrapped[1] = 2; // Mode 2 = LZ
                 uint32_t rc = (uint32_t)pal_data.size();
                 std::memcpy(&wrapped[2], &rc, 4);
                 wrapped.insert(wrapped.end(), lz.begin(), lz.end());

                 tl_lossless_mode_debug_stats_.palette_lz_used_count++;
                 tl_lossless_mode_debug_stats_.palette_lz_saved_bytes_sum += (pal_data.size() - wrapped.size());

                 pal_data = std::move(wrapped);
             }
        }

        std::vector<uint8_t> cpy_data = CopyCodec::encode_copy_stream(copy_ops);

        // Phase 9l-1: Tile-local LZ for Copy Stream
        if (!cpy_data.empty()) {
            std::vector<uint8_t> lz = TileLZ::compress(cpy_data);
            size_t wrapped_size = 6 + lz.size();
            // Apply only if size reduction >= 2% (wrapped * 100 <= raw * 98)
            if (wrapped_size * 100 <= cpy_data.size() * 98) {
                std::vector<uint8_t> wrapped;
                wrapped.resize(6);
                wrapped[0] = FileHeader::WRAPPER_MAGIC_COPY;
                wrapped[1] = 2; // Mode 2 = LZ
                uint32_t rc = (uint32_t)cpy_data.size();
                std::memcpy(&wrapped[2], &rc, 4);
                wrapped.insert(wrapped.end(), lz.begin(), lz.end());
                
                tl_lossless_mode_debug_stats_.copy_lz_used_count++;
                tl_lossless_mode_debug_stats_.copy_lz_saved_bytes_sum += (cpy_data.size() - wrapped.size());
                
                cpy_data = std::move(wrapped);
            }
        }
        std::vector<uint8_t> pindex_data;

        std::vector<uint8_t> cfl_data = build_cfl_payload(cfl_params);

        auto dc_stream = encode_tokens(dc_tokens, build_cdf(dc_tokens));
        std::vector<uint8_t> tile_data;
        if (use_band_group_cdf) {
            constexpr size_t kBandPindexMinStreamBytes = 32 * 1024;  // avoid overhead on tiny AC bands
            std::vector<uint8_t> pindex_low, pindex_mid, pindex_high;
            auto ac_low_stream = encode_tokens(
                ac_low_tokens, build_cdf(ac_low_tokens), pi ? &pindex_low : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            auto ac_mid_stream = encode_tokens(
                ac_mid_tokens, build_cdf(ac_mid_tokens), pi ? &pindex_mid : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            auto ac_high_stream = encode_tokens(
                ac_high_tokens, build_cdf(ac_high_tokens), pi ? &pindex_high : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            pindex_data = serialize_band_pindex_blob(pindex_low, pindex_mid, pindex_high);
            // TileHeader v3 (lossy): 10 fields (40 bytes)
            uint32_t sz[10] = {
                (uint32_t)dc_stream.size(), 
                (uint32_t)ac_low_stream.size(), 
                (uint32_t)ac_mid_stream.size(),
                (uint32_t)ac_high_stream.size(),
                (uint32_t)pindex_data.size(), 
                (uint32_t)q_deltas.size(), 
                (uint32_t)cfl_data.size(),
                (uint32_t)bt_data.size(),
                (uint32_t)pal_data.size(), 
                (uint32_t)cpy_data.size()
            };
            tile_data.resize(40); std::memcpy(&tile_data[0], sz, 40);
            tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
            tile_data.insert(tile_data.end(), ac_low_stream.begin(), ac_low_stream.end());
            tile_data.insert(tile_data.end(), ac_mid_stream.begin(), ac_mid_stream.end());
            tile_data.insert(tile_data.end(), ac_high_stream.begin(), ac_high_stream.end());
            if (sz[4]>0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
            if (sz[5]>0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[5]); }
            if (sz[6]>0) { tile_data.insert(tile_data.end(), cfl_data.begin(), cfl_data.end()); }
            if (sz[7]>0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
            if (sz[8]>0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
            if (sz[9]>0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        } else {
            auto ac_stream = encode_tokens(
                ac_tokens, build_cdf(ac_tokens), pi ? &pindex_data : nullptr,
                target_pindex_meta_ratio_percent
            );
            // TileHeader v2 (legacy): 8 fields (32 bytes)
            uint32_t sz[8] = {
                (uint32_t)dc_stream.size(),
                (uint32_t)ac_stream.size(),
                (uint32_t)pindex_data.size(),
                (uint32_t)q_deltas.size(),
                (uint32_t)cfl_data.size(),
                (uint32_t)bt_data.size(),
                (uint32_t)pal_data.size(),
                (uint32_t)cpy_data.size()
            };
            tile_data.resize(32); std::memcpy(&tile_data[0], sz, 32);
            tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
            tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
            if (sz[2] > 0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
            if (sz[3] > 0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[3]); }
            if (sz[4] > 0) { tile_data.insert(tile_data.end(), cfl_data.begin(), cfl_data.end()); }
            if (sz[5] > 0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
            if (sz[6] > 0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
            if (sz[7] > 0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        }

        return tile_data;
    }

    static CDFTable build_cdf(const std::vector<Token>& t) { std::vector<uint32_t> f(76, 1); for (const auto& x : t) { int sym = static_cast<int>(x.type); if (sym < 76) f[sym]++; } return CDFBuilder().build_from_freq(f); }
    static int calculate_pindex_interval(
        size_t token_count,
        size_t encoded_token_stream_bytes,
        int target_meta_ratio_percent = 2
    ) {
        if (token_count == 0 || encoded_token_stream_bytes == 0) return 4096;
        target_meta_ratio_percent = std::clamp(target_meta_ratio_percent, 1, 10);
        double target_meta_bytes = (double)encoded_token_stream_bytes * (double)target_meta_ratio_percent / 100.0;
        // P-Index serialization: 12-byte header + 40 bytes/checkpoint.
        double target_checkpoints = (target_meta_bytes - 12.0) / 40.0;
        if (target_checkpoints < 1.0) target_checkpoints = 1.0;
        double raw_interval = (double)token_count / target_checkpoints;
        int interval = (int)std::llround(raw_interval);
        interval = std::clamp(interval, 64, 4096);
        interval = ((interval + 7) / 8) * 8;  // PIndexBuilder expects 8-aligned token interval.
        return std::clamp(interval, 64, 4096);
    }

    static std::vector<uint8_t> serialize_band_pindex_blob(
        const std::vector<uint8_t>& low,
        const std::vector<uint8_t>& mid,
        const std::vector<uint8_t>& high
    ) {
        if (low.empty() && mid.empty() && high.empty()) return {};
        std::vector<uint8_t> out;
        out.resize(12);
        uint32_t low_sz = (uint32_t)low.size();
        uint32_t mid_sz = (uint32_t)mid.size();
        uint32_t high_sz = (uint32_t)high.size();
        std::memcpy(&out[0], &low_sz, 4);
        std::memcpy(&out[4], &mid_sz, 4);
        std::memcpy(&out[8], &high_sz, 4);
        out.insert(out.end(), low.begin(), low.end());
        out.insert(out.end(), mid.begin(), mid.end());
        out.insert(out.end(), high.begin(), high.end());
        return out;
    }

    static std::vector<uint8_t> encode_tokens(
        const std::vector<Token>& t,
        const CDFTable& c,
        std::vector<uint8_t>* out_pi = nullptr,
        int target_pindex_meta_ratio_percent = 2,
        size_t min_pindex_stream_bytes = 0
    ) {
        std::vector<uint8_t> output; int alpha = c.alphabet_size; std::vector<uint8_t> cdf_data(alpha * 4);
        for (int i = 0; i < alpha; i++) { uint32_t f = c.freq[i]; std::memcpy(&cdf_data[i * 4], &f, 4); }
        uint32_t cdf_size = cdf_data.size(); output.resize(4); std::memcpy(output.data(), &cdf_size, 4); output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        uint32_t token_count = t.size(); size_t count_offset = output.size(); output.resize(count_offset + 4); std::memcpy(&output[count_offset], &token_count, 4);
        FlatInterleavedEncoder encoder; for (const auto& tok : t) encoder.encode_symbol(c, static_cast<uint8_t>(tok.type)); auto rb = encoder.finish();
        uint32_t rans_size = rb.size(); size_t rs_offset = output.size(); output.resize(rs_offset + 4); std::memcpy(&output[rs_offset], &rans_size, 4); output.insert(output.end(), rb.begin(), rb.end());
        std::vector<uint8_t> raw_data; uint32_t raw_count = 0;
        for (const auto& tok : t) if (tok.raw_bits_count > 0) { raw_data.push_back(tok.raw_bits_count); raw_data.push_back(tok.raw_bits & 0xFF); raw_data.push_back((tok.raw_bits >> 8) & 0xFF); raw_count++; }
        size_t rc_offset = output.size(); output.resize(rc_offset + 4); std::memcpy(&output[rc_offset], &raw_count, 4); output.insert(output.end(), raw_data.begin(), raw_data.end());
        if (out_pi) {
            if (t.empty() || output.size() < min_pindex_stream_bytes) {
                out_pi->clear();
            } else {
                int interval = calculate_pindex_interval(
                    t.size(), output.size(), target_pindex_meta_ratio_percent
                );
                auto pindex = PIndexBuilder::build(rb, c, t.size(), (uint32_t)interval);
                *out_pi = PIndexCodec::serialize(pindex);
            }
        }
        return output;
    }

    static std::vector<uint8_t> pad_image(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h) {
        std::vector<uint8_t> padded(pad_w * pad_h);
        for (uint32_t y = 0; y < pad_h; y++) for (uint32_t x = 0; x < pad_w; x++) padded[y * pad_w + x] = pixels[std::min(y, height - 1) * width + std::min(x, width - 1)];
        return padded;
    }
    static void extract_block(const uint8_t* pixels, uint32_t stride, uint32_t height, int bx, int by, int16_t block[64]) {
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) block[y * 8 + x] = static_cast<int16_t>(pixels[(by * 8 + y) * stride + (bx * 8 + x)]) - 128;
    }

public:
    static std::vector<uint8_t> encode_block_types(
        const std::vector<FileHeader::BlockType>& types,
        bool allow_compact = false
    ) {
        std::vector<uint8_t> raw;
        // Loop generating `raw` (RLE bytes)
        int current_type = -1;
        int current_run = 0;
        for (auto t : types) {
            int type = (int)t;
            if (type != current_type) {
                if (current_run > 0) {
                    while (current_run > 0) {
                        int run = std::min(current_run, 64);
                        raw.push_back((uint8_t)((current_type & 0x03) | ((run - 1) << 2)));
                        current_run -= run;
                    }
                }
                current_type = type;
                current_run = 1;
            } else {
                current_run++;
            }
        }
        if (current_run > 0) {
            while (current_run > 0) {
                int run = std::min(current_run, 64);
                raw.push_back((uint8_t)((current_type & 0x03) | ((run - 1) << 2)));
                current_run -= run;
            }
        }
        
        if (!allow_compact) return raw;

        // Candidate 1: Raw RLE (Legacy) -> `raw`
        // Candidate 2: Mode 1 (rANS)
        // Replaced encode_tokens with encode_byte_stream (Fixes H1)
        auto mode1_payload = encode_byte_stream(raw);
        
        // Candidate 3: Mode 2 (LZ)
        std::vector<uint8_t> mode2_payload = TileLZ::compress(raw);
        
        size_t size_raw = raw.size();
        size_t size_mode1 = 6 + mode1_payload.size(); // [0xA6][1][raw_cnt][payload]
        size_t size_mode2 = 6 + mode2_payload.size(); // [0xA6][2][raw_cnt][payload]
        
        // Select Best
        // Bias: LZ/rANS must be < Raw * 0.98 ?
        size_t best_size = size_raw;
        int best_mode = 0; // 0=Raw
        
        if (size_mode1 < best_size && size_mode1 * 100 <= size_raw * 98) {
            best_size = size_mode1;
            best_mode = 1;
        }
        if (size_mode2 < best_size && size_mode2 * 100 <= size_raw * 98) {
             best_size = size_mode2;
             best_mode = 2;
        }
        
        // Apply
        if (best_mode == 1) {
             std::vector<uint8_t> out;
             out.resize(6);
             out[0] = FileHeader::WRAPPER_MAGIC_BLOCK_TYPES;
             out[1] = 1;
             uint32_t rc = (uint32_t)size_raw;
             std::memcpy(&out[2], &rc, 4);
             out.insert(out.end(), mode1_payload.begin(), mode1_payload.end());
             return out;
        } else if (best_mode == 2) {
             std::vector<uint8_t> out;
             out.resize(6);
             out[0] = FileHeader::WRAPPER_MAGIC_BLOCK_TYPES;
             out[1] = 2;
             uint32_t rc = (uint32_t)size_raw;
             std::memcpy(&out[2], &rc, 4);
             out.insert(out.end(), mode2_payload.begin(), mode2_payload.end());
             
             tl_lossless_mode_debug_stats_.block_types_lz_used_count++;
             tl_lossless_mode_debug_stats_.block_types_lz_saved_bytes_sum += (size_raw - out.size());
             
             return out;
        }
        
        return raw;
    }


    static void accumulate_palette_stream_diagnostics(
        const std::vector<uint8_t>& pal_raw,
        LosslessModeDebugStats& s
    ) {
        if (pal_raw.empty()) return;

        s.palette_stream_raw_bytes_sum += pal_raw.size();

        size_t pos = 0;
        bool is_v2 = false;
        bool is_v3 = false;
        bool is_v4 = false;
        uint8_t flags = 0;
        auto bits_for_palette_size = [](int p_size) -> int {
            if (p_size <= 1) return 0;
            if (p_size <= 2) return 1;
            if (p_size <= 4) return 2;
            return 3;
        };

        auto fail = [&]() {
            s.palette_parse_errors++;
            return;
        };

        if (pal_raw[0] == 0x40 || pal_raw[0] == 0x41 || pal_raw[0] == 0x42) {
            is_v2 = true;
            is_v3 = (pal_raw[0] == 0x41 || pal_raw[0] == 0x42);
            is_v4 = (pal_raw[0] == 0x42);
            if (is_v3) s.palette_stream_v3_count++;
            else s.palette_stream_v2_count++;
            pos = 1;
            if (pos >= pal_raw.size()) return fail();
            flags = pal_raw[pos++];

            if (flags & 0x01) {
                if (pos >= pal_raw.size()) return fail();
                uint8_t dict_count = pal_raw[pos++];
                s.palette_stream_mask_dict_count++;
                s.palette_stream_mask_dict_entries += dict_count;
                size_t need = (size_t)dict_count * 8;
                if (pos + need > pal_raw.size()) return fail();
                pos += need;
            }

            if (is_v3 && (flags & 0x02)) {
                if (pos >= pal_raw.size()) return fail();
                uint8_t pal_dict_count = pal_raw[pos++];
                s.palette_stream_palette_dict_count++;
                s.palette_stream_palette_dict_entries += pal_dict_count;
                for (uint8_t i = 0; i < pal_dict_count; i++) {
                    if (pos >= pal_raw.size()) return fail();
                    uint8_t psz = pal_raw[pos++];
                    if (psz == 0 || psz > 8) return fail();
                    size_t color_bytes = (size_t)psz * (is_v4 ? 2 : 1);
                    if (pos + color_bytes > pal_raw.size()) return fail();
                    pos += color_bytes;
                }
            }
        }

        while (pos < pal_raw.size()) {
            uint8_t head = pal_raw[pos++];
            bool use_prev = (head & 0x80) != 0;
            bool use_dict_ref = is_v3 && !use_prev && ((head & 0x40) != 0);
            int p_size = (head & 0x07) + 1;

            s.palette_blocks_parsed++;
            if (use_prev) s.palette_blocks_prev_reuse++;
            else if (use_dict_ref) s.palette_blocks_dict_ref++;
            else s.palette_blocks_raw_colors++;

            if (p_size <= 2) s.palette_blocks_two_color++;
            else s.palette_blocks_multi_color++;

            if (!use_prev) {
                if (use_dict_ref) {
                    if (pos >= pal_raw.size()) return fail();
                    pos += 1;
                } else {
                    size_t color_bytes = (size_t)p_size * (is_v4 ? 2 : 1);
                    if (pos + color_bytes > pal_raw.size()) return fail();
                    pos += color_bytes;
                }
            }

            if (!is_v2 || p_size <= 1) continue;

            if (p_size == 2) {
                size_t need = (flags & 0x01) ? 1 : 8;
                if (pos + need > pal_raw.size()) return fail();
                pos += need;
                continue;
            }

            int bits = bits_for_palette_size(p_size);
            size_t idx_bytes = (size_t)((64 * bits + 7) / 8);
            if (pos + idx_bytes > pal_raw.size()) return fail();
            pos += idx_bytes;
        }
    }

    // ========================================================================
    // Lossless encoding
    // ========================================================================

    /**
     * Encode a grayscale image losslessly.
     */
    static std::vector<uint8_t> encode_lossless(const uint8_t* pixels, uint32_t width, uint32_t height) {
        reset_lossless_mode_debug_stats();

        FileHeader header;
        header.width = width; header.height = height;
        header.bit_depth = 8; header.num_channels = 1;
        header.colorspace = 2; // RGB (grayscale)
        header.subsampling = 0; header.tile_cols = 1; header.tile_rows = 1;
        header.quality = 0;    // 0 = lossless
        header.flags |= 1;    // bit0 = lossless
        header.pindex_density = 0;

        // Convert to int16_t plane
        std::vector<int16_t> plane(width * height);
        for (uint32_t i = 0; i < width * height; i++) {
            plane[i] = (int16_t)pixels[i];
        }

        auto profile = classify_lossless_profile(plane.data(), width, height);
        auto tile_data = encode_plane_lossless(plane.data(), width, height, profile);

        // Build file: Header + ChunkDir + Tile
        ChunkDirectory dir;
        dir.add("TIL0", 0, tile_data.size());
        auto dir_data = dir.serialize();
        size_t tile_offset = 48 + dir_data.size();
        dir.entries[0].offset = tile_offset;
        dir_data = dir.serialize();

        std::vector<uint8_t> output;
        output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        return output;
    }

    /**
     * Encode a color image losslessly using YCoCg-R.
     */
    static std::vector<uint8_t> encode_color_lossless(const uint8_t* rgb_data, uint32_t width, uint32_t height) {
        reset_lossless_mode_debug_stats();

        // RGB -> YCoCg-R
        std::vector<int16_t> y_plane(width * height);
        std::vector<int16_t> co_plane(width * height);
        std::vector<int16_t> cg_plane(width * height);

        for (uint32_t i = 0; i < width * height; i++) {
            rgb_to_ycocg_r(rgb_data[i * 3], rgb_data[i * 3 + 1], rgb_data[i * 3 + 2],
                            y_plane[i], co_plane[i], cg_plane[i]);
        }

        auto profile = classify_lossless_profile(y_plane.data(), width, height);
        auto tile_y  = encode_plane_lossless(y_plane.data(), width, height, profile);
        auto tile_co = encode_plane_lossless(co_plane.data(), width, height, profile);
        auto tile_cg = encode_plane_lossless(cg_plane.data(), width, height, profile);

        FileHeader header;
        header.width = width; header.height = height;
        header.bit_depth = 8; header.num_channels = 3;
        header.colorspace = 1; // YCoCg-R
        header.subsampling = 0; // 4:4:4 (no subsampling for lossless)
        header.tile_cols = 1; header.tile_rows = 1;
        header.quality = 0;
        header.flags |= 1;
        header.pindex_density = 0;

        ChunkDirectory dir;
        dir.add("TIL0", 0, tile_y.size());
        dir.add("TIL1", 0, tile_co.size());
        dir.add("TIL2", 0, tile_cg.size());
        auto dir_data = dir.serialize();

        size_t offset = 48 + dir_data.size();
        dir.entries[0].offset = offset; offset += tile_y.size();
        dir.entries[1].offset = offset; offset += tile_co.size();
        dir.entries[2].offset = offset;
        dir_data = dir.serialize();

        std::vector<uint8_t> output;
        output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), tile_y.begin(), tile_y.end());
        output.insert(output.end(), tile_co.begin(), tile_co.end());
        output.insert(output.end(), tile_cg.begin(), tile_cg.end());
        return output;
    }

    // ------------------------------------------------------------------------
    // Lossless mode bit estimators (coarse heuristics for mode decision only)
    // Units: 1 unit = 0.5 bits (scaled by 2)
    // ------------------------------------------------------------------------
    static int estimate_copy_bits(const CopyParams& cp, int tile_width, LosslessProfile profile) {
        (void)tile_width;
        int bits2 = 4;  // block_type (2 bits * 2)
        int small_idx = CopyCodec::small_vector_index(cp);
        if (small_idx >= 0) {
            bits2 += 4;  // small-vector code (2 bits * 2)
            bits2 += 4;  // amortized stream/mode overhead (2 bits * 2)
        } else {
            bits2 += 64; // raw dx/dy payload fallback (32 bits * 2)
        }
        
        // Profile-based bias
        if (profile == LosslessProfile::PHOTO) bits2 += 8; // +4 bits
        else if (profile == LosslessProfile::ANIME) bits2 += 6; // +3 bits
        // UI: +0 bits

        return bits2;
    }

    static int estimate_palette_index_bits_per_pixel(int palette_size) {
        if (palette_size <= 1) return 0;
        if (palette_size <= 2) return 1;
        if (palette_size <= 4) return 2;
        return 3;
    }

    static int estimate_palette_bits(const Palette& p, int transitions, LosslessProfile profile) {
        if (p.size == 0) return std::numeric_limits<int>::max();
        int bits2 = 4;   // block_type (2 bits * 2)
        bits2 += 16;     // per-block palette header (8 bits * 2)
        int wide_colors = 0;
        for (int i = 0; i < p.size; i++) {
            if (p.colors[i] < -128 || p.colors[i] > 127) wide_colors++;
        }
        bits2 += ((int)p.size - wide_colors) * 16; // 8 bits * 2
        bits2 += wide_colors * 32;                 // 16 bits * 2

        if (p.size <= 1) return bits2;
        if (p.size == 2) {
            // 2-color blocks are mask-based in PaletteCodec.
            // Lower transitions usually compress better via dictionary reuse.
            bits2 += (transitions <= 24) ? 48 : 128; // (24/64 bits * 2)
            if (profile != LosslessProfile::PHOTO && transitions <= 16) bits2 -= 16; // screen bias
            return bits2;
        }

        int bits_per_index = estimate_palette_index_bits_per_pixel((int)p.size);
        bits2 += 64 * bits_per_index * 2;
        if (profile != LosslessProfile::PHOTO) {
            // UI/Anime often have repeated local index patterns that the palette stream dictionary
            // and wrappers compress better than this coarse estimate suggests.
            if (transitions <= 16) bits2 -= 96;
            else if (transitions <= 24) bits2 -= 64;
            else if (transitions <= 32) bits2 -= 32;
        } else {
            // Keep photo mode conservative for >2-color palettes.
            bits2 += ((int)p.size - wide_colors) * 16;
            bits2 += wide_colors * 32;
        }
        return bits2;
    }

    static int estimate_filter_symbol_bits2(int abs_residual, LosslessProfile profile) {
        if (abs_residual == 0) return (profile == LosslessProfile::PHOTO) ? 1 : 2;  // 0.5 bits (photo) / 1.0 bits (default)
        if (abs_residual <= 1) return 4;  // 2 bits * 2
        if (abs_residual <= 3) return 6;  // 3 bits * 2
        if (abs_residual <= 7) return 8;  // 4 bits * 2
        if (abs_residual <= 15) return 10; // 5 bits * 2
        if (abs_residual <= 31) return 12; // 6 bits * 2
        if (abs_residual <= 63) return 14; // 7 bits * 2
        if (abs_residual <= 127) return 16; // 8 bits * 2
        return 20; // 10 bits * 2
    }

    static int lossless_filter_candidates(LosslessProfile profile) {
        // Keep MED only for photo-like profile to avoid UI/anime regressions.
        return (profile == LosslessProfile::PHOTO) ? LosslessFilter::FILTER_COUNT : LosslessFilter::FILTER_MED;
    }

    static int estimate_filter_bits(
        const int16_t* padded, uint32_t pad_w, uint32_t pad_h, int cur_x, int cur_y, LosslessProfile profile
    ) {
        (void)pad_h;
        int best_bits2 = std::numeric_limits<int>::max();
        const int filter_count = lossless_filter_candidates(profile);
        for (int f = 0; f < filter_count; f++) {
            int bits2 = 4; // block_type (2 bits * 2)
            bits2 += 6;    // effective filter_id overhead (3 bits * 2)
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int px = cur_x + x;
                    int py = cur_y + y;
                    int16_t orig = padded[py * (int)pad_w + px];
                    int16_t a = (px > 0) ? padded[py * (int)pad_w + (px - 1)] : 0;
                    int16_t b = (py > 0) ? padded[(py - 1) * (int)pad_w + px] : 0;
                    int16_t c = (px > 0 && py > 0) ? padded[(py - 1) * (int)pad_w + (px - 1)] : 0;
                    int16_t pred = 0;
                    switch (f) {
                        case 0: pred = 0; break;
                        case 1: pred = a; break;
                        case 2: pred = b; break;
                        case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                        case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                        case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                    }
                    int abs_r = std::abs((int)orig - (int)pred);
                    bits2 += estimate_filter_symbol_bits2(abs_r, profile);
                }
            }
            best_bits2 = std::min(best_bits2, bits2);
        }
        return best_bits2;
    }

    static int bits_for_symbol_count(int count) {
        if (count <= 1) return 0;
        int bits = 0;
        int v = 1;
        while (v < count) {
            v <<= 1;
            bits++;
        }
        return bits;
    }

    static std::vector<uint8_t> pack_index_bits(const std::vector<uint8_t>& indices, int bits) {
        std::vector<uint8_t> out;
        if (bits <= 0 || indices.empty()) return out;
        out.reserve((indices.size() * (size_t)bits + 7) / 8);
        uint64_t acc = 0;
        int acc_bits = 0;
        const uint32_t mask = (1u << bits) - 1u;
        for (uint8_t idx : indices) {
            acc |= (uint64_t)((uint32_t)idx & mask) << acc_bits;
            acc_bits += bits;
            while (acc_bits >= 8) {
                out.push_back((uint8_t)(acc & 0xFFu));
                acc >>= 8;
                acc_bits -= 8;
            }
        }
        if (acc_bits > 0) out.push_back((uint8_t)(acc & 0xFFu));
        return out;
    }

    // Screen-profile v1 candidate:
    // [0xAD][mode:u8][bits:u8][reserved:u8][palette_count:u16][pixel_count:u32][raw_packed_size:u32]
    // [palette:int16 * palette_count][payload]
    // mode=0: raw packed index bytes, mode=1: rANS(payload), mode=2: LZ(payload)
    static std::vector<uint8_t> encode_plane_lossless_screen_indexed_tile(
        const int16_t* plane, uint32_t width, uint32_t height
    ) {
        if (!plane || width == 0 || height == 0) return {};
        uint32_t pad_w = ((width + 7) / 8) * 8;
        uint32_t pad_h = ((height + 7) / 8) * 8;
        const uint32_t pixel_count = pad_w * pad_h;
        if (pixel_count == 0) return {};

        std::unordered_map<int16_t, uint32_t> freq;
        freq.reserve(128);
        for (uint32_t y = 0; y < pad_h; y++) {
            uint32_t sy = std::min(y, height - 1);
            for (uint32_t x = 0; x < pad_w; x++) {
                uint32_t sx = std::min(x, width - 1);
                int16_t v = plane[sy * width + sx];
                freq[v]++;
                if (freq.size() > 64) return {};
            }
        }
        if (freq.empty()) return {};

        std::vector<std::pair<int16_t, uint32_t>> freq_pairs(freq.begin(), freq.end());
        std::sort(freq_pairs.begin(), freq_pairs.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        if (freq_pairs.size() > 64) return {};

        std::vector<int16_t> palette_vals;
        palette_vals.reserve(freq_pairs.size());
        std::unordered_map<int16_t, uint8_t> val_to_idx;
        val_to_idx.reserve(freq_pairs.size() * 2);
        for (size_t i = 0; i < freq_pairs.size(); i++) {
            palette_vals.push_back(freq_pairs[i].first);
            val_to_idx[freq_pairs[i].first] = (uint8_t)i;
        }

        const int bits_per_index = bits_for_symbol_count((int)palette_vals.size());
        std::vector<uint8_t> indices;
        indices.reserve(pixel_count);
        for (uint32_t y = 0; y < pad_h; y++) {
            uint32_t sy = std::min(y, height - 1);
            for (uint32_t x = 0; x < pad_w; x++) {
                uint32_t sx = std::min(x, width - 1);
                int16_t v = plane[sy * width + sx];
                auto it = val_to_idx.find(v);
                if (it == val_to_idx.end()) return {};
                indices.push_back(it->second);
            }
        }

        auto packed = pack_index_bits(indices, bits_per_index);
        std::vector<uint8_t> payload = packed;
        uint8_t mode = 0;

        if (!packed.empty()) {
            auto packed_rans = encode_byte_stream(packed);
            if (!packed_rans.empty() && packed_rans.size() < payload.size()) {
                payload = std::move(packed_rans);
                mode = 1;
            }

            auto packed_lz = TileLZ::compress(packed);
            if (!packed_lz.empty() && packed_lz.size() < payload.size()) {
                payload = std::move(packed_lz);
                mode = 2;
            }
        }

        std::vector<uint8_t> out;
        out.reserve(14 + palette_vals.size() * 2 + payload.size());
        out.push_back(FileHeader::WRAPPER_MAGIC_SCREEN_INDEXED);
        out.push_back(mode);
        out.push_back((uint8_t)bits_per_index);
        out.push_back(0);
        uint16_t pcount = (uint16_t)palette_vals.size();
        out.push_back((uint8_t)(pcount & 0xFF));
        out.push_back((uint8_t)((pcount >> 8) & 0xFF));
        out.push_back((uint8_t)(pixel_count & 0xFF));
        out.push_back((uint8_t)((pixel_count >> 8) & 0xFF));
        out.push_back((uint8_t)((pixel_count >> 16) & 0xFF));
        out.push_back((uint8_t)((pixel_count >> 24) & 0xFF));
        uint32_t raw_packed_size = (uint32_t)packed.size();
        out.push_back((uint8_t)(raw_packed_size & 0xFF));
        out.push_back((uint8_t)((raw_packed_size >> 8) & 0xFF));
        out.push_back((uint8_t)((raw_packed_size >> 16) & 0xFF));
        out.push_back((uint8_t)((raw_packed_size >> 24) & 0xFF));

        for (int16_t v : palette_vals) {
            uint16_t uv = (uint16_t)v;
            out.push_back((uint8_t)(uv & 0xFF));
            out.push_back((uint8_t)((uv >> 8) & 0xFF));
        }
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    /**
     * Encode a single int16_t plane losslessly with Screen Profile support.
     * 
     * Hybrid block-based pipeline:
     *   1. Classify each 8x8 block: Palette -> Copy -> Filter
     *   2. Custom row-level filtering (full image context, Palette/Copy as anchors)
     *   3. Filter block residuals -> zigzag -> split lo/hi -> rANS (data-adaptive CDF)
     *
     * Tile format v2 (32-byte header):
     *   [4B filter_ids_size][4B lo_stream_size][4B hi_stream_size][4B filter_pixel_count]
     *   [4B block_types_size][4B palette_data_size][4B copy_data_size][4B reserved]
     *   [filter_ids][lo_stream][hi_stream][block_types][palette_data][copy_data]
     */
    // Backward compatibility wrapper
    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height, bool use_photo_mode_bias
    ) {
        return encode_plane_lossless(data, width, height,
            use_photo_mode_bias ? LosslessProfile::PHOTO : LosslessProfile::UI);
    }

    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height, LosslessProfile profile = LosslessProfile::UI
    ) {
        // Pad dimensions to multiple of 8
        uint32_t pad_w = ((width + 7) / 8) * 8;
        uint32_t pad_h = ((height + 7) / 8) * 8;
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;

        // Phase 9s-5: Telemetry
        if (profile == LosslessProfile::UI) tl_lossless_mode_debug_stats_.profile_ui_tiles++;
        else if (profile == LosslessProfile::ANIME) tl_lossless_mode_debug_stats_.profile_anime_tiles++;
        else tl_lossless_mode_debug_stats_.profile_photo_tiles++;

        // Pad the int16_t image
        std::vector<int16_t> padded(pad_w * pad_h, 0);
        for (uint32_t y = 0; y < pad_h; y++) {
            for (uint32_t x = 0; x < pad_w; x++) {
                padded[y * pad_w + x] = data[std::min(y, height - 1) * width + std::min(x, width - 1)];
            }
        }

        // --- Step 1: Block classification ---
        std::vector<FileHeader::BlockType> block_types(nb, FileHeader::BlockType::DCT); // DCT = Filter for lossless
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        std::vector<CopyParams> copy_ops;
        const CopyParams kLosslessCopyCandidates[4] = {
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
        };

        const CopyParams kTileMatch4Candidates[16] = {
            CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
            CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
            CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
        };
        struct Tile4Result {
            uint8_t indices[4];
        };
        std::vector<Tile4Result> tile4_results;
        
        struct LosslessModeParams {
            int palette_max_colors = 2;
            int palette_transition_limit = 63;
            int64_t palette_variance_limit = 1040384;
        } mode_params;
        
        if (profile == LosslessProfile::UI) {
            mode_params.palette_max_colors = 8;
            mode_params.palette_transition_limit = 58;
            mode_params.palette_variance_limit = 2621440;
        } else if (profile == LosslessProfile::ANIME) {
            mode_params.palette_max_colors = 8; // Fixed from 12 to match Palette struct size
            mode_params.palette_transition_limit = 62;
            mode_params.palette_variance_limit = 4194304;
        }
        // PHOTO uses default (2 colors, tight variance)

        FileHeader::BlockType prev_mode = FileHeader::BlockType::DCT;

        for (int i = 0; i < nb; i++) {
            int bx = i % nx, by = i / nx;
            int cur_x = bx * 8;
            int cur_y = by * 8;

            int16_t block[64];
            int64_t sum = 0, sum_sq = 0;
            int transitions = 0;
            int palette_transitions = 0;
            int unique_cnt = 0;

            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int idx = y * 8 + x;
                    int16_t v = padded[(cur_y + y) * pad_w + (cur_x + x)];
                    block[idx] = v;
                    sum += v;
                    sum_sq += (int64_t)v * (int64_t)v;
                    if (idx > 0 && block[idx - 1] != v) transitions++;
                }
            }

            {
                int16_t vals[64];
                std::memcpy(vals, block, sizeof(vals));
                std::sort(vals, vals + 64);
                unique_cnt = 1;
                for (int k = 1; k < 64; k++) {
                    if (vals[k] != vals[k - 1]) unique_cnt++;
                }
            }

            int64_t variance_proxy = sum_sq - ((sum * sum) / 64); // 64 * variance

            // Copy candidate (exact match only).
            bool copy_found = false;
            CopyParams copy_candidate;
            if (i > 0) {
                for (const auto& cand : kLosslessCopyCandidates) {
                    int src_x = cur_x + cand.dx;
                    int src_y = cur_y + cand.dy;
                    if (src_x < 0 || src_y < 0) continue;
                    if (src_x + 7 >= (int)pad_w || src_y + 7 >= (int)pad_h) continue;
                    if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                    bool match = true;
                    for (int y = 0; y < 8 && match; y++) {
                        for (int x = 0; x < 8; x++) {
                            if (padded[(cur_y + y) * pad_w + (cur_x + x)] !=
                                padded[(src_y + y) * pad_w + (src_x + x)]) {
                                match = false;
                                break;
                            }
                        }
                    }
                    if (match) {
                        copy_found = true;
                        copy_candidate = cand;
                        break;
                    }
                }
            }

            // Palette candidate.
            bool palette_found = false;
            Palette palette_candidate;
            std::vector<uint8_t> palette_index_candidate;
            palette_transitions = transitions;
            if (unique_cnt <= mode_params.palette_max_colors) {
                palette_candidate = PaletteExtractor::extract(block, mode_params.palette_max_colors);
                if (palette_candidate.size > 0 && palette_candidate.size <= mode_params.palette_max_colors) {
                    bool transition_ok = (transitions <= mode_params.palette_transition_limit) || (palette_candidate.size <= 1);
                    bool variance_ok = variance_proxy <= mode_params.palette_variance_limit;
                    if (transition_ok && variance_ok) {
                        palette_found = true;
                        palette_index_candidate = PaletteExtractor::map_indices(block, palette_candidate);
                        palette_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (palette_index_candidate[(size_t)k] != palette_index_candidate[(size_t)k - 1]) {
                                palette_transitions++;
                            }
                        }
                    }
                }
            }

            // TileMatch4 candidate (4x4 x 4 quadrants)
            bool tile4_found = false;
            Tile4Result tile4_candidate;
            {
                int matches = 0;
                for (int q = 0; q < 4; q++) {
                    int qx = (q % 2) * 4;
                    int qy = (q / 2) * 4;
                    int cur_qx = cur_x + qx;
                    int cur_qy = cur_y + qy;

                    bool q_match_found = false;
                    for (int cand_idx = 0; cand_idx < 16; cand_idx++) {
                        const auto& cand = kTileMatch4Candidates[cand_idx];
                        int src_x = cur_qx + cand.dx;
                        int src_y = cur_qy + cand.dy;

                        // Bounds and causality check
                        if (src_x < 0 || src_y < 0 || src_x + 3 >= (int)pad_w || src_y + 3 >= (int)pad_h) continue;
                        if (!(src_y < cur_qy || (src_y == cur_qy && src_x < cur_qx))) continue;

                        bool match = true;
                        for (int dy = 0; dy < 4 && match; dy++) {
                            for (int dx = 0; dx < 4; dx++) {
                                if (padded[(cur_qy + dy) * pad_w + (cur_qx + dx)] !=
                                    padded[(src_y + dy) * pad_w + (src_x + dx)]) {
                                    match = false;
                                    break;
                                }
                            }
                        }
                        if (match) {
                            tile4_candidate.indices[q] = (uint8_t)cand_idx;
                            q_match_found = true;
                            break;
                        }
                    }
                    if (q_match_found) matches++;
                    else break;
                }
                if (matches == 4) tile4_found = true;
            }

            // Mode decision:
            // Choose the minimum estimated bits among TILE_MATCH4 / Copy / Palette / Filter.
            int tile4_bits2 = std::numeric_limits<int>::max();
            int copy_bits2 = std::numeric_limits<int>::max();
            int palette_bits2 = std::numeric_limits<int>::max();
            int filter_bits2 = estimate_filter_bits(
                padded.data(), pad_w, pad_h, cur_x, cur_y, profile
            );
            auto& mode_stats = tl_lossless_mode_debug_stats_;
            if (tile4_found) {
                tile4_bits2 = 36; // 2 bit mode + 4x4 bit indices = 18 bits (36 units)
            }
            if (copy_found) {
                copy_bits2 = estimate_copy_bits(copy_candidate, (int)pad_w, profile);
            }

            // Phase 9t-2: Palette rescue for UI/ANIME.
            // If strict palette gates reject a block but a palette still beats filter
            // by a clear margin, re-enable palette candidate for this block.
            if (!palette_found && profile != LosslessProfile::PHOTO && unique_cnt <= 8) {
                Palette rescue_palette = PaletteExtractor::extract(block, 8);
                if (rescue_palette.size > 0 && rescue_palette.size <= 8) {
                    mode_stats.palette_rescue_attempted++;
                    auto rescue_indices = PaletteExtractor::map_indices(block, rescue_palette);
                    int rescue_transitions = 0;
                    for (int k = 1; k < 64; k++) {
                        if (rescue_indices[(size_t)k] != rescue_indices[(size_t)k - 1]) {
                            rescue_transitions++;
                        }
                    }
                    int rescue_bits2 = estimate_palette_bits(rescue_palette, rescue_transitions, profile);
                    if (profile == LosslessProfile::ANIME &&
                        rescue_palette.size >= 2 && rescue_transitions <= 60) {
                        rescue_bits2 -= 24;
                    }
                    if (rescue_bits2 + 8 < filter_bits2) {
                        palette_found = true;
                        palette_candidate = rescue_palette;
                        palette_index_candidate = std::move(rescue_indices);
                        palette_transitions = rescue_transitions;
                        mode_stats.palette_rescue_adopted++;
                        mode_stats.palette_rescue_gain_bits_sum +=
                            (uint64_t)std::max(0, (filter_bits2 - rescue_bits2) / 2);
                    }
                }
            }

            if (palette_found) {
                palette_bits2 = estimate_palette_bits(
                    palette_candidate, palette_transitions, profile
                );
                // Phase 9s-6: Anime-specific palette bias
                if (profile == LosslessProfile::ANIME && palette_candidate.size >= 2 && palette_transitions <= 60) {
                    palette_bits2 -= 24;
                    tl_lossless_mode_debug_stats_.anime_palette_bonus_applied++;
                }

                // Phase 9t-2: Palette rescue bias for UI/ANIME-like flat regions.
                // Guarded by high variance proxy to avoid Photo/Natural regressions.
                const bool rescue_bias_cond =
                    (profile != LosslessProfile::PHOTO) &&
                    (palette_candidate.size <= 8) &&
                    (unique_cnt <= 8) &&
                    (palette_transitions <= 32) &&
                    (variance_proxy >= 30000);
                if (rescue_bias_cond) {
                    mode_stats.palette_rescue_attempted++;
                    palette_bits2 -= 32; // 16-bit rescue bias
                }
            }

            if (profile == LosslessProfile::PHOTO) {
                // P0: Mode Inertia (-2 bits = -4 units)
                if (tile4_found && prev_mode == FileHeader::BlockType::TILE_MATCH4) tile4_bits2 -= 4;
                if (copy_found && prev_mode == FileHeader::BlockType::COPY) copy_bits2 -= 4;
                if (palette_found && prev_mode == FileHeader::BlockType::PALETTE) palette_bits2 -= 4;
                if (prev_mode == FileHeader::BlockType::DCT) filter_bits2 -= 4;
            }

            mode_stats.total_blocks++;
            mode_stats.est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);
            if (tile4_found) {
                mode_stats.tile4_candidates++;
                mode_stats.est_tile4_bits_sum += (uint64_t)(tile4_bits2 / 2);
            }
            if (copy_found) {
                mode_stats.copy_candidates++;
                mode_stats.est_copy_bits_sum += (uint64_t)(copy_bits2 / 2);
            }
            if (palette_found) {
                mode_stats.palette_candidates++;
                mode_stats.est_palette_bits_sum += (uint64_t)(palette_bits2 / 2);
            }
            if (copy_found && palette_found) mode_stats.copy_palette_overlap++;

            FileHeader::BlockType best_mode = FileHeader::BlockType::DCT;
            if (tile4_bits2 <= copy_bits2 && tile4_bits2 <= palette_bits2 && tile4_bits2 <= filter_bits2) {
                best_mode = FileHeader::BlockType::TILE_MATCH4;
            } else if (copy_bits2 <= palette_bits2 && copy_bits2 <= filter_bits2) {
                best_mode = FileHeader::BlockType::COPY;
            } else if (palette_bits2 <= filter_bits2) {
                best_mode = FileHeader::BlockType::PALETTE;
            }
            int selected_bits2 = filter_bits2;
            if (best_mode == FileHeader::BlockType::TILE_MATCH4) selected_bits2 = tile4_bits2;
            else if (best_mode == FileHeader::BlockType::COPY) selected_bits2 = copy_bits2;
            else if (best_mode == FileHeader::BlockType::PALETTE) selected_bits2 = palette_bits2;

            // Diagnostics: candidate existed but lost to another mode.
            if (tile4_found && best_mode != FileHeader::BlockType::TILE_MATCH4) {
                if (best_mode == FileHeader::BlockType::COPY) mode_stats.tile4_rejected_by_copy++;
                else if (best_mode == FileHeader::BlockType::PALETTE) mode_stats.tile4_rejected_by_palette++;
                else mode_stats.tile4_rejected_by_filter++;
                mode_stats.est_tile4_loss_bits_sum +=
                    (uint64_t)(std::max(0, tile4_bits2 - selected_bits2) / 2);
            }
            if (copy_found && best_mode != FileHeader::BlockType::COPY) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) mode_stats.copy_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::PALETTE) mode_stats.copy_rejected_by_palette++;
                else mode_stats.copy_rejected_by_filter++;
                mode_stats.est_copy_loss_bits_sum +=
                    (uint64_t)(std::max(0, copy_bits2 - selected_bits2) / 2);
            }
            if (palette_found && best_mode != FileHeader::BlockType::PALETTE) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) mode_stats.palette_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::COPY) mode_stats.palette_rejected_by_copy++;
                else mode_stats.palette_rejected_by_filter++;
                mode_stats.est_palette_loss_bits_sum +=
                    (uint64_t)(std::max(0, palette_bits2 - selected_bits2) / 2);
            }

            block_types[i] = best_mode;
            prev_mode = best_mode;
            if (best_mode == FileHeader::BlockType::TILE_MATCH4) {
                tile4_results.push_back(tile4_candidate);
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                mode_stats.tile4_selected++;
            } else if (best_mode == FileHeader::BlockType::COPY) {
                mode_stats.copy_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                copy_ops.push_back(copy_candidate);
            } else if (best_mode == FileHeader::BlockType::PALETTE) {
                mode_stats.palette_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                palettes.push_back(palette_candidate);
                palette_indices.push_back(std::move(palette_index_candidate));
                // Count rescue adoption for biased palette paths.
                if (profile != LosslessProfile::PHOTO &&
                    palette_candidate.size <= 8 &&
                    unique_cnt <= 8 &&
                    palette_transitions <= 32 &&
                    variance_proxy >= 30000) {
                    mode_stats.palette_rescue_adopted++;
                    mode_stats.palette_rescue_gain_bits_sum += 16;
                }
            } else {
                mode_stats.filter_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);

                // Phase 9t-1: collect detailed diagnostics for filter-selected blocks.
                if (copy_found) mode_stats.filter_blocks_with_copy_candidate++;
                if (palette_found) mode_stats.filter_blocks_with_palette_candidate++;
                if (unique_cnt <= 2) mode_stats.filter_blocks_unique_le2++;
                else if (unique_cnt <= 4) mode_stats.filter_blocks_unique_le4++;
                else if (unique_cnt <= 8) mode_stats.filter_blocks_unique_le8++;
                else mode_stats.filter_blocks_unique_gt8++;
                mode_stats.filter_blocks_transitions_sum += (uint64_t)transitions;
                mode_stats.filter_blocks_variance_proxy_sum +=
                    (uint64_t)std::max<int64_t>(0, variance_proxy);
                mode_stats.filter_blocks_est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);

                // Diagnose whether palette(<=8, current palette struct limit)
                // could beat filter on this block.
                if (unique_cnt <= 8) {
                    Palette diag_palette16 = PaletteExtractor::extract(block, 8);
                    if (diag_palette16.size > 0 && diag_palette16.size <= 8) {
                        auto diag_indices = PaletteExtractor::map_indices(block, diag_palette16);
                        int diag_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (diag_indices[(size_t)k] != diag_indices[(size_t)k - 1]) {
                                diag_transitions++;
                            }
                        }
                        int diag_palette_bits2 = estimate_palette_bits(diag_palette16, diag_transitions, profile);
                        if (profile == LosslessProfile::ANIME &&
                            diag_palette16.size >= 2 && diag_transitions <= 60) {
                            diag_palette_bits2 -= 24;
                        }
                        mode_stats.filter_diag_palette16_candidates++;
                        mode_stats.filter_diag_palette16_size_sum += diag_palette16.size;
                        mode_stats.filter_diag_palette16_est_bits_sum += (uint64_t)(diag_palette_bits2 / 2);
                        if (diag_palette_bits2 < filter_bits2) {
                            mode_stats.filter_diag_palette16_better++;
                            mode_stats.filter_diag_palette16_gain_bits_sum +=
                                (uint64_t)((filter_bits2 - diag_palette_bits2) / 2);
                        }
                    }
                }
            }
            // Filter mode keeps default DCT tag.
        }

        // --- Step 2: Custom filtering (block-type aware, full image context) ---
        // For each row: select best filter (considering Filter-block pixels only),
        // compute residuals for Filter-block pixels only.
        // Prediction context uses original pixel values — Palette/Copy pixels
        // serve as perfect anchors for prediction.
        std::vector<uint8_t> filter_ids(pad_h);
        std::vector<int16_t> filter_residuals;

        for (uint32_t y = 0; y < pad_h; y++) {
            int by_row = y / 8;

            // Check if this row has any filter blocks
            bool has_filter = false;
            for (int bx = 0; bx < nx; bx++) {
                if (block_types[by_row * nx + bx] == FileHeader::BlockType::DCT) {
                    has_filter = true;
                    break;
                }
            }
            if (!has_filter) {
                filter_ids[y] = 0;
                continue;
            }


            // Try all filters, pick one minimizing sum(|residual|) for filter-block pixels
            int best_f = 0;
            int64_t best_sum = INT64_MAX;
            const int filter_count = lossless_filter_candidates(profile);
            for (int f = 0; f < filter_count; f++) {
                int64_t sum = 0;
                for (uint32_t x = 0; x < pad_w; x++) {
                    int bx_col = x / 8;
                    if (block_types[by_row * nx + bx_col] != FileHeader::BlockType::DCT) continue;
                    int16_t orig = padded[y * pad_w + x];
                    int16_t a = (x > 0) ? padded[y * pad_w + x - 1] : 0;
                    int16_t b = (y > 0) ? padded[(y - 1) * pad_w + x] : 0;
                    int16_t c = (x > 0 && y > 0) ? padded[(y - 1) * pad_w + x - 1] : 0;
                    int16_t pred;
                    switch (f) {
                        case 0: pred = 0; break;
                        case 1: pred = a; break;
                        case 2: pred = b; break;
                        case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                        case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                        case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                        default: pred = 0; break;
                    }
                    sum += std::abs((int)(orig - pred));
                }
                if (sum < best_sum) { best_sum = sum; best_f = f; }
            }
            filter_ids[y] = (uint8_t)best_f;
            tl_lossless_mode_debug_stats_.filter_rows_with_pixels++;
            if (best_f >= 0 && best_f < 6) {
                tl_lossless_mode_debug_stats_.filter_row_id_hist[best_f]++;
            }
            if (best_f == 5) tl_lossless_mode_debug_stats_.filter_med_selected++;

            // Emit residuals for filter-block pixels only
            for (uint32_t x = 0; x < pad_w; x++) {
                int bx_col = x / 8;
                if (block_types[by_row * nx + bx_col] != FileHeader::BlockType::DCT) continue;
                int16_t orig = padded[y * pad_w + x];
                int16_t a = (x > 0) ? padded[y * pad_w + x - 1] : 0;
                int16_t b = (y > 0) ? padded[(y - 1) * pad_w + x] : 0;
                int16_t c = (x > 0 && y > 0) ? padded[(y - 1) * pad_w + x - 1] : 0;
                int16_t pred;
                switch (best_f) {
                    case 0: pred = 0; break;
                    case 1: pred = a; break;
                    case 2: pred = b; break;
                    case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                    case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                    case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                    default: pred = 0; break;
                }
                filter_residuals.push_back(orig - pred);
            }
        }

        // --- Step 3: ZigZag + rANS encode filter residuals (data-adaptive CDF) ---
        std::vector<uint8_t> lo_stream, hi_stream;
        uint32_t filter_pixel_count = (uint32_t)filter_residuals.size();

        if (!filter_residuals.empty()) {
            std::vector<uint8_t> lo_bytes(filter_pixel_count), hi_bytes(filter_pixel_count);
            for (size_t i = 0; i < filter_pixel_count; i++) {
                uint16_t zz = zigzag_encode_val(filter_residuals[i]);
                lo_bytes[i] = (uint8_t)(zz & 0xFF);
                hi_bytes[i] = (uint8_t)((zz >> 8) & 0xFF);
            }
            // --- filter_lo wrapper (legacy / delta+rANS / LZ / LZ+rANS / predictors) ---
            auto lo_legacy = encode_byte_stream(lo_bytes);
            tl_lossless_mode_debug_stats_.filter_lo_raw_bytes_sum += lo_bytes.size();

            size_t legacy_size = lo_legacy.size();
            
            // Phase 9u-tune: Expose tuning knobs
            constexpr int kFilterLoModeWrapperGainPermilleDefault = 990;
            constexpr int kFilterLoMode5GainPermille = 995;
            constexpr int kFilterLoMode5MinRawBytes = 2048; // Gated for larger payloads
            constexpr int kFilterLoMode5MinLZBytes = 1024;  // Gated to pay for rANS table cost

            // Candidate 1: Mode 1 (delta transform + rANS)
            std::vector<uint8_t> delta_bytes(lo_bytes.size());
            delta_bytes[0] = lo_bytes[0];
            for (size_t i = 1; i < lo_bytes.size(); i++) {
                delta_bytes[i] = (uint8_t)(lo_bytes[i] - lo_bytes[i - 1]); // mod 256
            }
            auto delta_rans = encode_byte_stream(delta_bytes);
            size_t delta_wrapped = 6 + delta_rans.size();

            // Candidate 2: Mode 2 (LZ compress)
            auto lo_lz = TileLZ::compress(lo_bytes);
            size_t lz_wrapped = 6 + lo_lz.size();
            
            // Candidate 3: Mode 5 (LZ output + rANS with shared/static CDF)
            std::vector<uint8_t> lo_lz_rans;
            size_t lz_rans_wrapped = std::numeric_limits<size_t>::max();
            if (lo_bytes.size() >= kFilterLoMode5MinRawBytes && lo_lz.size() >= kFilterLoMode5MinLZBytes) {
                tl_lossless_mode_debug_stats_.filter_lo_mode5_candidates++;
                lo_lz_rans = encode_byte_stream_shared_lz(lo_lz);
                lz_rans_wrapped = 6 + lo_lz_rans.size();
                tl_lossless_mode_debug_stats_.filter_lo_mode5_candidate_bytes_sum += lo_lz.size();
                tl_lossless_mode_debug_stats_.filter_lo_mode5_wrapped_bytes_sum += lz_rans_wrapped;
                tl_lossless_mode_debug_stats_.filter_lo_mode5_legacy_bytes_sum += legacy_size;
            }

            // Select best mode with per-mode gating
            int best_mode = 0;
            size_t best_size = legacy_size;

            // Gate Mode 1
            if (delta_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
                if (delta_wrapped < best_size) {
                    best_size = delta_wrapped;
                    best_mode = 1;
                }
            }

            // Gate Mode 2
            if (lz_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
                tl_lossless_mode_debug_stats_.filter_lo_mode2_candidate_bytes_sum += lz_wrapped;
                if (lz_wrapped < best_size) {
                    best_size = lz_wrapped;
                    best_mode = 2;
                }
            } else {
                tl_lossless_mode_debug_stats_.filter_lo_mode2_reject_gate++;
            }

            // Gate Mode 5
            if (lz_rans_wrapped != std::numeric_limits<size_t>::max()) {
                // Rule: Must be better than legacy AND at least 1% better than LZ-only (Mode 2)
                bool better_than_legacy = (lz_rans_wrapped * 1000 <= legacy_size * kFilterLoMode5GainPermille);
                bool better_than_lz = (lz_rans_wrapped * 100 <= lz_wrapped * 99); 

                if (better_than_legacy && better_than_lz) {
                    if (lz_rans_wrapped < best_size) {
                        best_size = lz_rans_wrapped;
                        best_mode = 5;
                    } else {
                        tl_lossless_mode_debug_stats_.filter_lo_mode5_reject_best++;
                    }
                } else {
                    tl_lossless_mode_debug_stats_.filter_lo_mode5_reject_gate++;
                }
            }

            // Mode 3: Row Predictor (Phase 9p) - For Photo and Anime
            std::vector<uint8_t> pred_stream;
            std::vector<uint8_t> resid_stream;
            std::vector<uint8_t> mode3_preds; // for telemetry
            std::vector<int> row_lens;

            // Mode 4: Context-split by filter_id (Phase 9q)
            std::vector<std::vector<uint8_t>> mode4_streams(6);
            std::vector<uint32_t> mode4_ctx_raw_counts(6, 0);

            if ((profile == LosslessProfile::PHOTO || profile == LosslessProfile::ANIME) &&
                lo_bytes.size() > 256) {
                // Reconstruct row lengths of filter pixels
                row_lens.assign(pad_h, 0);
                for (uint32_t y = 0; y < pad_h; y++) {
                    int count = 0;
                    int row_idx = y / 8;
                    for (int bx = 0; bx < nx; bx++) {
                        if (block_types[row_idx * nx + bx] == FileHeader::BlockType::DCT) {
                            count += 8;
                        }
                    }
                    row_lens[y] = count;
                }

                std::vector<uint8_t> preds;
                std::vector<uint8_t> resids;
                resids.reserve(lo_bytes.size());
                
                size_t offset = 0;
                size_t prev_valid_row_start = 0;
                size_t prev_valid_row_len = 0;

                for (uint32_t y = 0; y < pad_h; y++) {
                    int len = row_lens[y];
                    if (len == 0) continue;

                    const uint8_t* curr_row = &lo_bytes[offset];
                    int best_p = 0;
                    int64_t min_cost = -1;

                    // Evaluate 4 predictors: 0=NONE, 1=SUB, 2=UP, 3=AVG
                    for (int p = 0; p < 4; p++) {
                        int64_t cost = 0;
                        for (int i = 0; i < len; i++) {
                            uint8_t pred_val = 0;
                            if (p == 1) { // SUB
                                pred_val = (i == 0) ? 0 : curr_row[i - 1];
                            } else if (p == 2) { // UP
                                pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                            } else if (p == 3) { // AVG
                                uint8_t left = (i == 0) ? 0 : curr_row[i - 1];
                                uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                                pred_val = (left + up) / 2;
                            }
                            int diff = (int)curr_row[i] - pred_val;
                            if (diff < 0) diff += 256;
                            // Cost: min(d, 256-d) aka circular distance
                            if (diff > 128) diff = 256 - diff;
                            cost += diff;
                        }
                        if (min_cost == -1 || cost < min_cost) {
                            min_cost = cost;
                            best_p = p;
                        }
                    }

                    preds.push_back((uint8_t)best_p);

                    // Generate residuals with best predictor
                    for (int i = 0; i < len; i++) {
                         uint8_t pred_val = 0;
                         if (best_p == 1) pred_val = (i == 0) ? 0 : curr_row[i - 1];
                         else if (best_p == 2) pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                         else if (best_p == 3) {
                             uint8_t left = (i == 0) ? 0 : curr_row[i - 1];
                             uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                             pred_val = (left + up) / 2;
                         }
                         resids.push_back((uint8_t)(curr_row[i] - pred_val));
                    }

                    prev_valid_row_start = offset;
                    prev_valid_row_len = len;
                    offset += len;
                }

                auto preds_enc = encode_byte_stream(preds);
                auto resids_enc = encode_byte_stream(resids);
                // [magic][mode][raw_count:4][pred_sz:4][preds][resids]
                size_t total_sz = 1 + 1 + 4 + 4 + preds_enc.size() + resids_enc.size();

                if (total_sz < best_size && total_sz * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
                    best_size = total_sz;
                    best_mode = 3;
                    pred_stream = std::move(preds_enc);
                    resid_stream = std::move(resids_enc);
                    mode3_preds = std::move(preds);
                }

                // Mode 4: split filter_lo by filter_id context and encode each independently.
                std::vector<std::vector<uint8_t>> lo_ctx(6);
                size_t off = 0;
                for (uint32_t y = 0; y < pad_h; y++) {
                    int len = row_lens[y];
                    if (len <= 0) continue;
                    size_t end_off = std::min(off + (size_t)len, lo_bytes.size());
                    if (end_off <= off) break;
                    uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
                    if (fid > 5) fid = 0;
                    lo_ctx[fid].insert(lo_ctx[fid].end(), lo_bytes.begin() + off, lo_bytes.begin() + end_off);
                    off = end_off;
                }

                size_t mode4_sz = 1 + 1 + 4 + 6 * 4; // magic + mode + raw_count + len[6]
                int nonempty_ctx = 0;
                std::vector<std::vector<uint8_t>> ctx_streams(6);
                std::vector<uint32_t> ctx_raw_counts(6, 0);
                for (int k = 0; k < 6; k++) {
                    ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
                    if (!lo_ctx[k].empty()) nonempty_ctx++;
                    if (!lo_ctx[k].empty()) {
                        ctx_streams[k] = encode_byte_stream(lo_ctx[k]);
                    }
                    mode4_sz += ctx_streams[k].size();
                }
                if (nonempty_ctx >= 2 && mode4_sz * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
                    tl_lossless_mode_debug_stats_.filter_lo_mode4_candidate_bytes_sum += mode4_sz;
                    if (mode4_sz < best_size) {
                        best_size = mode4_sz;
                        best_mode = 4;
                        mode4_streams = std::move(ctx_streams);
                        mode4_ctx_raw_counts = std::move(ctx_raw_counts);
                    }
                } else if (nonempty_ctx >= 2) {
                    tl_lossless_mode_debug_stats_.filter_lo_mode4_reject_gate++;
                }
            }

            if (best_mode == 0) {
                lo_stream = std::move(lo_legacy);
                tl_lossless_mode_debug_stats_.filter_lo_mode0++;
            } else if (best_mode == 3) {
                // Build wrapper: [0xAB][mode=3][raw_count:4][pred_sz:4][preds][resids]
                lo_stream.clear();
                lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
                lo_stream.push_back(3); // mode=3
                
                uint32_t rc = (uint32_t)lo_bytes.size();
                lo_stream.push_back((uint8_t)(rc & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));

                uint32_t ps = (uint32_t)pred_stream.size();
                lo_stream.push_back((uint8_t)(ps & 0xFF));
                lo_stream.push_back((uint8_t)((ps >> 8) & 0xFF));
                lo_stream.push_back((uint8_t)((ps >> 16) & 0xFF));
                lo_stream.push_back((uint8_t)((ps >> 24) & 0xFF));
                
                lo_stream.insert(lo_stream.end(), pred_stream.begin(), pred_stream.end());
                lo_stream.insert(lo_stream.end(), resid_stream.begin(), resid_stream.end());

                tl_lossless_mode_debug_stats_.filter_lo_mode3++;
                tl_lossless_mode_debug_stats_.filter_lo_mode3_rows_sum += mode3_preds.size();
                if (lo_legacy.size() > lo_stream.size()) {
                    tl_lossless_mode_debug_stats_.filter_lo_mode3_saved_bytes_sum += (lo_legacy.size() - lo_stream.size());
                }
                for (uint8_t p : mode3_preds) {
                    if (p < 4) tl_lossless_mode_debug_stats_.filter_lo_mode3_pred_hist[p]++;
                }
            } else if (best_mode == 4) {
                // Build wrapper: [0xAB][mode=4][raw_count:4][len0..len5][stream0..stream5]
                lo_stream.clear();
                lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
                lo_stream.push_back(4); // mode=4
                uint32_t rc = (uint32_t)lo_bytes.size();
                lo_stream.push_back((uint8_t)(rc & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
                for (int k = 0; k < 6; k++) {
                    uint32_t len = (uint32_t)mode4_streams[k].size();
                    lo_stream.push_back((uint8_t)(len & 0xFF));
                    lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
                    lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
                    lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
                }
                for (int k = 0; k < 6; k++) {
                    lo_stream.insert(lo_stream.end(), mode4_streams[k].begin(), mode4_streams[k].end());
                }
                tl_lossless_mode_debug_stats_.filter_lo_mode4++;
                if (lo_legacy.size() > lo_stream.size()) {
                    tl_lossless_mode_debug_stats_.filter_lo_mode4_saved_bytes_sum +=
                        (uint64_t)(lo_legacy.size() - lo_stream.size());
                }
                int nonempty_ctx = 0;
                for (int k = 0; k < 6; k++) {
                    tl_lossless_mode_debug_stats_.filter_lo_ctx_bytes_sum[k] += mode4_ctx_raw_counts[k];
                    if (mode4_ctx_raw_counts[k] > 0) nonempty_ctx++;
                }
                if (nonempty_ctx > 0) {
                    tl_lossless_mode_debug_stats_.filter_lo_ctx_nonempty_tiles++;
                }
            } else {
                // Build wrapper: [0xAB][mode][raw_count:4B LE][payload]
                lo_stream.clear();
                lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
                lo_stream.push_back((uint8_t)best_mode);
                uint32_t rc = (uint32_t)lo_bytes.size();
                lo_stream.push_back((uint8_t)(rc & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
                lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));

                if (best_mode == 1) {
                    lo_stream.insert(lo_stream.end(), delta_rans.begin(), delta_rans.end());
                    tl_lossless_mode_debug_stats_.filter_lo_mode1++;
                } else if (best_mode == 2) {
                    lo_stream.insert(lo_stream.end(), lo_lz.begin(), lo_lz.end());
                    tl_lossless_mode_debug_stats_.filter_lo_mode2++;
                } else {
                    lo_stream.insert(lo_stream.end(), lo_lz_rans.begin(), lo_lz_rans.end());
                    tl_lossless_mode_debug_stats_.filter_lo_mode5++;
                    if (lo_legacy.size() > lo_stream.size()) {
                        tl_lossless_mode_debug_stats_.filter_lo_mode5_saved_bytes_sum +=
                            (uint64_t)(lo_legacy.size() - lo_stream.size());
                    }
                }
            }
            tl_lossless_mode_debug_stats_.filter_lo_compressed_bytes_sum += lo_stream.size();

            // --- Phase 9n: filter_hi sparse mode ---
            size_t zero_count = 0;
            for (uint8_t b : hi_bytes) if (b == 0) zero_count++;
            double zero_ratio = (double)zero_count / (double)hi_bytes.size();
            tl_lossless_mode_debug_stats_.filter_hi_raw_bytes_sum += hi_bytes.size();
            tl_lossless_mode_debug_stats_.filter_hi_zero_ratio_sum += (uint64_t)(zero_ratio * 100.0);

            auto hi_rans = encode_byte_stream(hi_bytes);

            if (zero_ratio >= 0.75 && hi_bytes.size() >= 32) {
                // Try sparse: [0xAA][nz_lo][nz_mid][nz_hi][zero_mask][nonzero_rANS]
                size_t mask_size = (hi_bytes.size() + 7) / 8;
                std::vector<uint8_t> zero_mask(mask_size, 0);
                std::vector<uint8_t> nonzero_vals;
                nonzero_vals.reserve(hi_bytes.size() - zero_count);

                for (size_t i = 0; i < hi_bytes.size(); i++) {
                    if (hi_bytes[i] != 0) {
                        zero_mask[i / 8] |= (1u << (i % 8));
                        nonzero_vals.push_back(hi_bytes[i]);
                    }
                }

                std::vector<uint8_t> sparse_stream;
                sparse_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_HI);
                uint32_t nz_count = (uint32_t)nonzero_vals.size();
                sparse_stream.push_back((uint8_t)(nz_count & 0xFF));
                sparse_stream.push_back((uint8_t)((nz_count >> 8) & 0xFF));
                sparse_stream.push_back((uint8_t)((nz_count >> 16) & 0xFF));
                sparse_stream.insert(sparse_stream.end(), zero_mask.begin(), zero_mask.end());

                if (!nonzero_vals.empty()) {
                    auto nz_rans = encode_byte_stream(nonzero_vals);
                    sparse_stream.insert(sparse_stream.end(), nz_rans.begin(), nz_rans.end());
                }

                if (sparse_stream.size() < hi_rans.size()) {
                    hi_stream = std::move(sparse_stream);
                    tl_lossless_mode_debug_stats_.filter_hi_sparse_count++;
                } else {
                    hi_stream = std::move(hi_rans);
                    tl_lossless_mode_debug_stats_.filter_hi_dense_count++;
                }
            } else {
                hi_stream = std::move(hi_rans);
                tl_lossless_mode_debug_stats_.filter_hi_dense_count++;
            }
            tl_lossless_mode_debug_stats_.filter_hi_compressed_bytes_sum += hi_stream.size();
        }

        // --- Step 4: Encode block types, palette, copy, tile4 ---
        // --- Step 4: Encode block types, palette, copy, tile4 ---
        std::vector<uint8_t> bt_data = encode_block_types(block_types, true);
        
        int reorder_trials = 0;
        int reorder_adopted = 0;
        std::vector<uint8_t> pal_raw = PaletteCodec::encode_palette_stream(
            palettes, palette_indices, true, &reorder_trials, &reorder_adopted
        );
        tl_lossless_mode_debug_stats_.palette_reorder_trials += reorder_trials;
        tl_lossless_mode_debug_stats_.palette_reorder_adopted += reorder_adopted;
        std::vector<uint8_t> pal_data = pal_raw;
        accumulate_palette_stream_diagnostics(pal_raw, tl_lossless_mode_debug_stats_);
        if (!pal_data.empty()) {
            // Optional wrappers:
            // [0xA7][mode=1][raw_count:u32][rANS payload]
            // [0xA7][mode=2][raw_count:u32][LZ payload]
            const size_t raw_size = pal_data.size();

            auto encoded_pal = encode_byte_stream(pal_data);
            if (!encoded_pal.empty()) {
                std::vector<uint8_t> compact_pal;
                compact_pal.reserve(1 + 1 + 4 + encoded_pal.size());
                compact_pal.push_back(FileHeader::WRAPPER_MAGIC_PALETTE);
                compact_pal.push_back(1);
                uint32_t raw_count = (uint32_t)pal_data.size();
                compact_pal.resize(compact_pal.size() + 4);
                std::memcpy(compact_pal.data() + 2, &raw_count, 4);
                compact_pal.insert(compact_pal.end(), encoded_pal.begin(), encoded_pal.end());
                if (compact_pal.size() < pal_data.size()) {
                    tl_lossless_mode_debug_stats_.palette_stream_compact_count++;
                    tl_lossless_mode_debug_stats_.palette_stream_compact_saved_bytes_sum +=
                        (uint64_t)(pal_data.size() - compact_pal.size());
                    pal_data = std::move(compact_pal);
                }
            }

            auto lz_pal = TileLZ::compress(pal_raw);
            if (!lz_pal.empty()) {
                std::vector<uint8_t> lz_wrapped;
                lz_wrapped.reserve(1 + 1 + 4 + lz_pal.size());
                lz_wrapped.push_back(FileHeader::WRAPPER_MAGIC_PALETTE);
                lz_wrapped.push_back(2);
                uint32_t raw_count = (uint32_t)pal_raw.size();
                lz_wrapped.resize(lz_wrapped.size() + 4);
                std::memcpy(lz_wrapped.data() + 2, &raw_count, 4);
                lz_wrapped.insert(lz_wrapped.end(), lz_pal.begin(), lz_pal.end());
                if (lz_wrapped.size() < pal_data.size()) {
                    tl_lossless_mode_debug_stats_.palette_lz_used_count++;
                    tl_lossless_mode_debug_stats_.palette_lz_saved_bytes_sum +=
                        (uint64_t)(raw_size - lz_wrapped.size());
                    pal_data = std::move(lz_wrapped);
                }
            }
        }
        std::vector<uint8_t> cpy_raw = CopyCodec::encode_copy_stream(copy_ops);
        std::vector<uint8_t> cpy_data = cpy_raw;
        int copy_wrapper_mode = 0; // 0=raw, 1=rANS wrapper, 2=LZ wrapper
        if (!cpy_raw.empty()) {
            auto cpy_rans = encode_byte_stream(cpy_raw);
            if (!cpy_rans.empty()) {
                std::vector<uint8_t> wrapped;
                wrapped.reserve(1 + 1 + 4 + cpy_rans.size());
                wrapped.push_back(FileHeader::WRAPPER_MAGIC_COPY);
                wrapped.push_back(1);
                uint32_t raw_count = (uint32_t)cpy_raw.size();
                wrapped.resize(wrapped.size() + 4);
                std::memcpy(wrapped.data() + 2, &raw_count, 4);
                wrapped.insert(wrapped.end(), cpy_rans.begin(), cpy_rans.end());
                if (wrapped.size() < cpy_data.size()) {
                    cpy_data = std::move(wrapped);
                    copy_wrapper_mode = 1;
                }
            }

            auto cpy_lz = TileLZ::compress(cpy_raw);
            if (!cpy_lz.empty()) {
                std::vector<uint8_t> wrapped;
                wrapped.reserve(1 + 1 + 4 + cpy_lz.size());
                wrapped.push_back(FileHeader::WRAPPER_MAGIC_COPY);
                wrapped.push_back(2);
                uint32_t raw_count = (uint32_t)cpy_raw.size();
                wrapped.resize(wrapped.size() + 4);
                std::memcpy(wrapped.data() + 2, &raw_count, 4);
                wrapped.insert(wrapped.end(), cpy_lz.begin(), cpy_lz.end());
                if (wrapped.size() < cpy_data.size()) {
                    cpy_data = std::move(wrapped);
                    copy_wrapper_mode = 2;
                }
            }

            if (copy_wrapper_mode == 2) {
                tl_lossless_mode_debug_stats_.copy_lz_used_count++;
                tl_lossless_mode_debug_stats_.copy_lz_saved_bytes_sum +=
                    (uint64_t)(cpy_raw.size() - cpy_data.size());
            }
        }
        std::vector<uint8_t> tile4_raw;
        for (const auto& res : tile4_results) {
            tile4_raw.push_back((uint8_t)((res.indices[1] << 4) | (res.indices[0] & 0x0F)));
            tile4_raw.push_back((uint8_t)((res.indices[3] << 4) | (res.indices[2] & 0x0F)));
        }
        std::vector<uint8_t> tile4_data = tile4_raw;
        int tile4_mode = 0; // 0=raw, 1=rANS, 2=LZ
        if (!tile4_raw.empty()) {
            auto tile4_rans = encode_byte_stream(tile4_raw);
            if (!tile4_rans.empty()) {
                std::vector<uint8_t> wrapped;
                wrapped.reserve(1 + 1 + 4 + tile4_rans.size());
                wrapped.push_back(FileHeader::WRAPPER_MAGIC_TILE4);
                wrapped.push_back(1);
                uint32_t raw_count = (uint32_t)tile4_raw.size();
                wrapped.resize(wrapped.size() + 4);
                std::memcpy(wrapped.data() + 2, &raw_count, 4);
                wrapped.insert(wrapped.end(), tile4_rans.begin(), tile4_rans.end());
                if (wrapped.size() < tile4_data.size()) {
                    tile4_data = std::move(wrapped);
                    tile4_mode = 1;
                }
            }

            auto tile4_lz = TileLZ::compress(tile4_raw);
            if (!tile4_lz.empty()) {
                std::vector<uint8_t> wrapped;
                wrapped.reserve(1 + 1 + 4 + tile4_lz.size());
                wrapped.push_back(FileHeader::WRAPPER_MAGIC_TILE4);
                wrapped.push_back(2);
                uint32_t raw_count = (uint32_t)tile4_raw.size();
                wrapped.resize(wrapped.size() + 4);
                std::memcpy(wrapped.data() + 2, &raw_count, 4);
                wrapped.insert(wrapped.end(), tile4_lz.begin(), tile4_lz.end());
                if (wrapped.size() < tile4_data.size()) {
                    tile4_data = std::move(wrapped);
                    tile4_mode = 2;
                }
            }
        }

        // Stream-level diagnostics for lossless mode decision tuning.
        {
            auto& s = tl_lossless_mode_debug_stats_;
            s.block_types_bytes_sum += bt_data.size();
            s.palette_stream_bytes_sum += pal_data.size();
            s.tile4_stream_bytes_sum += tile4_data.size();

            for (uint8_t v : bt_data) {
                int run = ((v >> 2) & 0x3F) + 1;
                uint8_t type = (v & 0x03);
                s.block_type_runs_sum++;
                if (run <= 2) s.block_type_short_runs++;
                if (run >= 16) s.block_type_long_runs++;
                switch (type) {
                    case 0: s.block_type_runs_dct++; break;
                    case 1: s.block_type_runs_palette++; break;
                    case 2: s.block_type_runs_copy++; break;
                    case 3: s.block_type_runs_tile4++; break;
                    default: break;
                }
            }

            s.copy_stream_bytes_sum += cpy_data.size();
            s.copy_ops_total += copy_ops.size();
            for (const auto& cp : copy_ops) {
                if (CopyCodec::small_vector_index(cp) >= 0) s.copy_ops_small++;
                else s.copy_ops_raw++;
            }
            if (!copy_ops.empty()) {
                if (copy_wrapper_mode == 1) s.copy_wrap_mode1++;
                else if (copy_wrapper_mode == 2) s.copy_wrap_mode2++;
                else s.copy_wrap_mode0++;
            }

            if (!copy_ops.empty() && !cpy_raw.empty()) {
                s.copy_stream_count++;
                uint8_t mode = cpy_raw[0];
                uint64_t payload_bits = 0;
                if (mode == 0) {
                    s.copy_stream_mode0++;
                    payload_bits = (uint64_t)copy_ops.size() * 32ull;
                } else if (mode == 1) {
                    s.copy_stream_mode1++;
                    payload_bits = (uint64_t)copy_ops.size() * 2ull;
                } else if (mode == 2) {
                    s.copy_stream_mode2++;
                    if (cpy_raw.size() >= 2) {
                        uint8_t used_mask = cpy_raw[1];
                        int used_count = CopyCodec::popcount4(used_mask);
                        int bits_dyn = CopyCodec::small_vector_bits(used_count);
                        if (bits_dyn == 0) s.copy_mode2_zero_bit_streams++;
                        s.copy_mode2_dynamic_bits_sum += (uint64_t)bits_dyn;
                        payload_bits = (uint64_t)copy_ops.size() * (uint64_t)std::max(0, bits_dyn);
                    }
                } else if (mode == 3) {
                    s.copy_stream_mode3++;
                    if (cpy_raw.size() >= 2) {
                        size_t num_tokens = cpy_raw.size() - 2; // header is 2 bytes
                        s.copy_mode3_run_tokens_sum += num_tokens;
                        // Parse tokens to count runs and long runs
                        for (size_t ti = 2; ti < cpy_raw.size(); ti++) {
                            int run = (cpy_raw[ti] & 0x3F) + 1;
                            s.copy_mode3_runs_sum += (uint64_t)run;
                            if (run >= 16) s.copy_mode3_long_runs++;
                        }
                    }
                    payload_bits = (uint64_t)(cpy_raw.size() - 2) * 8ull; // tokens are payload
                }
                uint64_t stream_bits = (uint64_t)cpy_data.size() * 8ull;
                s.copy_stream_payload_bits_sum += payload_bits;
                s.copy_stream_overhead_bits_sum += (stream_bits > payload_bits) ? (stream_bits - payload_bits) : 0ull;
            }

            s.tile4_stream_raw_bytes_sum += tile4_raw.size();
            if (!tile4_raw.empty()) {
                if (tile4_mode == 1) s.tile4_stream_mode1++;
                else if (tile4_mode == 2) s.tile4_stream_mode2++;
                else s.tile4_stream_mode0++;
            }
        }

        // --- Step 5: Compress filter_ids (Phase 9n) ---
        tl_lossless_mode_debug_stats_.filter_ids_raw_bytes_sum += filter_ids.size();
        std::vector<uint8_t> filter_ids_packed;

        if (filter_ids.size() >= 8) {
            // Try rANS
            auto fid_rans = encode_byte_stream(filter_ids);
            size_t rans_wrapped_size = 2 + fid_rans.size(); // [magic][mode=1][data]

            // Try LZ
            auto fid_lz = TileLZ::compress(filter_ids);
            size_t lz_wrapped_size = 2 + fid_lz.size(); // [magic][mode=2][data]

            size_t raw_size = filter_ids.size();
            size_t best_size = raw_size;
            int best_mode = 0;

            if (rans_wrapped_size < best_size) { best_size = rans_wrapped_size; best_mode = 1; }
            if (lz_wrapped_size < best_size)   { best_size = lz_wrapped_size; best_mode = 2; }

            if (best_mode == 1) {
                filter_ids_packed.push_back(FileHeader::WRAPPER_MAGIC_FILTER_IDS);
                filter_ids_packed.push_back(1); // mode=rANS
                filter_ids_packed.insert(filter_ids_packed.end(), fid_rans.begin(), fid_rans.end());
                tl_lossless_mode_debug_stats_.filter_ids_mode1++;
            } else if (best_mode == 2) {
                filter_ids_packed.push_back(FileHeader::WRAPPER_MAGIC_FILTER_IDS);
                filter_ids_packed.push_back(2); // mode=LZ
                filter_ids_packed.insert(filter_ids_packed.end(), fid_lz.begin(), fid_lz.end());
                tl_lossless_mode_debug_stats_.filter_ids_mode2++;
            } else {
                filter_ids_packed = filter_ids; // raw
                tl_lossless_mode_debug_stats_.filter_ids_mode0++;
            }
        } else {
            filter_ids_packed = filter_ids; // too small to wrap
            tl_lossless_mode_debug_stats_.filter_ids_mode0++;
        }
        tl_lossless_mode_debug_stats_.filter_ids_compressed_bytes_sum += filter_ids_packed.size();

        // --- Step 6: Pack tile data (32-byte header) ---
        uint32_t hdr[8] = {
            (uint32_t)filter_ids_packed.size(),
            (uint32_t)lo_stream.size(),
            (uint32_t)hi_stream.size(),
            filter_pixel_count,
            (uint32_t)bt_data.size(),
            (uint32_t)pal_data.size(),
            (uint32_t)cpy_data.size(),
            (uint32_t)tile4_data.size() // used reserved hdr[7]
        };

        std::vector<uint8_t> tile_data;
        tile_data.resize(32);
        std::memcpy(tile_data.data(), hdr, 32);
        tile_data.insert(tile_data.end(), filter_ids_packed.begin(), filter_ids_packed.end());
        tile_data.insert(tile_data.end(), lo_stream.begin(), lo_stream.end());
        tile_data.insert(tile_data.end(), hi_stream.begin(), hi_stream.end());
        if (!bt_data.empty()) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
        if (!pal_data.empty()) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
        if (!cpy_data.empty()) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        if (!tile4_data.empty()) tile_data.insert(tile_data.end(), tile4_data.begin(), tile4_data.end());

        // Phase 9s-3: Enhanced Gating for Screen-Indexed Mode
        tl_lossless_mode_debug_stats_.screen_candidate_count++;

        bool screen_pre_gate_pass = true;
        // 1. Pre-gate: Reject if photo mode (means low copy hit rate)
        if (profile == LosslessProfile::PHOTO) screen_pre_gate_pass = false;

        // 2. Pre-gate: Reject small tiles (overhead dominates)
        if (width * height < 4096) screen_pre_gate_pass = false;

        if (!screen_pre_gate_pass) {
            tl_lossless_mode_debug_stats_.screen_rejected_pre_gate++;
        } else {
            auto screen_tile = encode_plane_lossless_screen_indexed_tile(data, width, height);
            
            if (screen_tile.empty() || screen_tile.size() < 14) {
                 tl_lossless_mode_debug_stats_.screen_rejected_pre_gate++;
            } else {
                 // Parse header to check properties
                 uint8_t screen_mode = screen_tile[1];
                 uint16_t palette_count = (uint16_t)screen_tile[4] | ((uint16_t)screen_tile[5] << 8);
                 uint32_t packed_size = (uint32_t)screen_tile[10] | ((uint32_t)screen_tile[11] << 8) |
                                        ((uint32_t)screen_tile[12] << 16) | ((uint32_t)screen_tile[13] << 24);

                 tl_lossless_mode_debug_stats_.screen_palette_count_sum += palette_count;
                 
                 int bits_per_index = 0;
                 if (palette_count <= 2) bits_per_index = 1;
                 else if (palette_count <= 4) bits_per_index = 2;
                 else if (palette_count <= 16) bits_per_index = 4;
                 else if (palette_count <= 64) bits_per_index = 6;
                 else bits_per_index = 8;
                 
                 tl_lossless_mode_debug_stats_.screen_bits_per_index_sum += bits_per_index;

                 // 3. Pre-gate strict checks
                 bool reject_strict = false;
                 if (palette_count > 48) reject_strict = true;
                 if (bits_per_index > 6) reject_strict = true; 
                 // Rate limit raw mode (mode=0) if it's large, to avoid bloat
                 if (screen_mode == 0 && packed_size > 2048) {
                      reject_strict = true;
                      tl_lossless_mode_debug_stats_.screen_mode0_reject_count++;
                 }

                 if (reject_strict) {
                     tl_lossless_mode_debug_stats_.screen_rejected_pre_gate++;
                 } else {
                      size_t legacy_size = tile_data.size();
                      size_t screen_size = screen_tile.size();
                      
                      // Cost-gate: Category detection
                      bool is_ui_like = (palette_count <= 24 && bits_per_index <= 5);
                      bool is_anime_like = !is_ui_like;
                      
                      if (is_ui_like) tl_lossless_mode_debug_stats_.screen_ui_like_count++;
                      else tl_lossless_mode_debug_stats_.screen_anime_like_count++;
                      
                      bool adopt = false;
                      if (is_ui_like) {
                          // UI: Require 1% gain
                          if (screen_size * 100ull <= legacy_size * 99ull) adopt = true;
                      } else {
                          // Anime: Require 2% gain (relaxed from 3% in phase 9s-5)
                          if (screen_size * 100ull <= legacy_size * 98ull) adopt = true;
                      }
                      
                      if (adopt) {
                          tl_lossless_mode_debug_stats_.screen_selected_count++;
                          if (legacy_size > screen_size)
                              tl_lossless_mode_debug_stats_.screen_gain_bytes_sum += (legacy_size - screen_size);
                          return screen_tile;
                      } else {
                          tl_lossless_mode_debug_stats_.screen_rejected_cost_gate++;
                          if (screen_size > legacy_size)
                              tl_lossless_mode_debug_stats_.screen_loss_bytes_sum += (screen_size - legacy_size);
                      }
                 }
            }
        }
        return tile_data;
    }


    /**
     * Encode a byte stream using rANS with data-adaptive CDF.
     * Format: [4B cdf_size][cdf_data][4B count][4B rans_size][rans_data]
     */
    static std::vector<uint8_t> encode_byte_stream(const std::vector<uint8_t>& bytes) {
        // Build frequency table (alphabet = 256)
        std::vector<uint32_t> freq(256, 1);  // Laplace smoothing
        for (uint8_t b : bytes) freq[b]++;

        CDFTable cdf = CDFBuilder().build_from_freq(freq);

        // Serialize CDF
        std::vector<uint8_t> cdf_data(256 * 4);
        for (int i = 0; i < 256; i++) {
            uint32_t f = cdf.freq[i];
            std::memcpy(&cdf_data[i * 4], &f, 4);
        }

        // Encode symbols
        FlatInterleavedEncoder encoder;
        for (uint8_t b : bytes) {
            encoder.encode_symbol(cdf, b);
        }
        auto rans_bytes = encoder.finish();

        // Pack: cdf_size + cdf + count + rans_size + rans
        std::vector<uint8_t> output;
        uint32_t cdf_size = (uint32_t)cdf_data.size();
        uint32_t count = (uint32_t)bytes.size();
        uint32_t rans_size = (uint32_t)rans_bytes.size();

        output.resize(4); std::memcpy(output.data(), &cdf_size, 4);
        output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        size_t off = output.size();
        output.resize(off + 4); std::memcpy(&output[off], &count, 4);
        off = output.size();
        output.resize(off + 4); std::memcpy(&output[off], &rans_size, 4);
        output.insert(output.end(), rans_bytes.begin(), rans_bytes.end());
        return output;
    }

    // Shared/static-CDF variant for Mode5 payload (TileLZ bytes).
    // Format: [4B count][4B rans_size][rans_data]
    static std::vector<uint8_t> encode_byte_stream_shared_lz(const std::vector<uint8_t>& bytes) {
        const CDFTable& cdf = get_mode5_shared_lz_cdf();

        FlatInterleavedEncoder encoder;
        for (uint8_t b : bytes) {
            encoder.encode_symbol(cdf, b);
        }
        auto rans_bytes = encoder.finish();

        std::vector<uint8_t> output;
        uint32_t count = (uint32_t)bytes.size();
        uint32_t rans_size = (uint32_t)rans_bytes.size();
        output.resize(8);
        std::memcpy(output.data(), &count, 4);
        std::memcpy(output.data() + 4, &rans_size, 4);
        output.insert(output.end(), rans_bytes.begin(), rans_bytes.end());
        return output;
    }

private:
    static const CDFTable& get_mode5_shared_lz_cdf() {
        static const CDFTable cdf = CDFBuilder().build_from_freq(mode5_shared_lz_freq());
        return cdf;
    }
};

} // namespace hakonyans
