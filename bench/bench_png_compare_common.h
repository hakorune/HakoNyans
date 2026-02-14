#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/codec/encode.h"

namespace bench_png_compare_common {

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

template <typename T>
T median_value(std::vector<T> v) {
    if (v.empty()) return T{};
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if ((n & 1u) != 0u) return v[n / 2];
    return (T)((v[n / 2 - 1] + v[n / 2]) / (T)2);
}

template <>
inline double median_value(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if ((n & 1u) != 0u) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline double ns_to_ms(uint64_t ns) {
    return (double)ns / 1000000.0;
}

inline bool parse_int_arg(const std::string& s, int* out) {
    try {
        size_t idx = 0;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return false;
        *out = v;
        return true;
    } catch (...) {
        return false;
    }
}

inline bool parse_lossless_preset_arg(
    const std::string& s, hakonyans::GrayscaleEncoder::LosslessPreset* out
) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back((char)std::tolower((unsigned char)c));
    }
    if (lower == "fast") {
        *out = hakonyans::GrayscaleEncoder::LosslessPreset::FAST;
        return true;
    }
    if (lower == "balanced") {
        *out = hakonyans::GrayscaleEncoder::LosslessPreset::BALANCED;
        return true;
    }
    if (lower == "max") {
        *out = hakonyans::GrayscaleEncoder::LosslessPreset::MAX;
        return true;
    }
    return false;
}

inline Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--base-dir" && i + 1 < argc) {
            args.base_dir = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            args.out_csv = argv[++i];
        } else if (a == "--baseline" && i + 1 < argc) {
            args.baseline_csv = argv[++i];
        } else if (a == "--runs" && i + 1 < argc) {
            int v = 0;
            if (!parse_int_arg(argv[++i], &v) || v <= 0) {
                throw std::runtime_error("--runs must be a positive integer");
            }
            args.runs = v;
        } else if (a == "--warmup" && i + 1 < argc) {
            int v = 0;
            if (!parse_int_arg(argv[++i], &v) || v < 0) {
                throw std::runtime_error("--warmup must be a non-negative integer");
            }
            args.warmup = v;
        } else if (a == "--preset" && i + 1 < argc) {
            hakonyans::GrayscaleEncoder::LosslessPreset preset{};
            if (!parse_lossless_preset_arg(argv[++i], &preset)) {
                throw std::runtime_error("--preset must be one of: fast, balanced, max");
            }
            args.preset = preset;
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--base-dir DIR] [--out CSV] [--baseline CSV] [--runs N] [--warmup N] [--preset fast|balanced|max]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + a);
        }
    }
    return args;
}

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

} // namespace bench_png_compare_common
