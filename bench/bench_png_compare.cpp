#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/codec/decode.h"
#include "../src/codec/encode.h"
#include "png_wrapper.h"
#include "ppm_loader.h"

using namespace hakonyans;

namespace {

struct EvalImage {
    std::string rel_path;
    std::string name;
};

const std::vector<EvalImage> kFixedEvalSet = {
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
    GrayscaleEncoder::LosslessPreset preset = GrayscaleEncoder::LosslessPreset::BALANCED;
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
double median_value(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if ((n & 1u) != 0u) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline double ns_to_ms(uint64_t ns) {
    return (double)ns / 1000000.0;
}

bool parse_int_arg(const std::string& s, int* out) {
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

bool parse_lossless_preset_arg(
    const std::string& s, GrayscaleEncoder::LosslessPreset* out
) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back((char)std::tolower((unsigned char)c));
    }
    if (lower == "fast") {
        *out = GrayscaleEncoder::LosslessPreset::FAST;
        return true;
    }
    if (lower == "balanced") {
        *out = GrayscaleEncoder::LosslessPreset::BALANCED;
        return true;
    }
    if (lower == "max") {
        *out = GrayscaleEncoder::LosslessPreset::MAX;
        return true;
    }
    return false;
}

Args parse_args(int argc, char** argv) {
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
            GrayscaleEncoder::LosslessPreset preset{};
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

std::vector<std::string> split_csv_line(const std::string& line) {
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

std::map<std::string, BaselineRow> load_baseline_csv(const std::string& path) {
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

void write_results_csv(const std::string& path, const std::vector<ResultRow>& rows) {
    std::filesystem::path p(path);
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to write CSV: " + path);
    }

    ofs << "image_id,image_name,width,height,hkn_bytes,png_bytes,png_over_hkn,dec_ms,natural_row_selected,natural_row_candidates,natural_row_selected_rate,gain_bytes,loss_bytes,hkn_enc_images_per_s,hkn_dec_images_per_s,png_enc_images_per_s,png_dec_images_per_s,hkn_enc_cpu_over_wall,hkn_dec_cpu_over_wall,hkn_enc_ms,hkn_dec_ms,png_enc_ms,png_dec_ms,hkn_enc_rgb_to_ycocg_ms,hkn_enc_profile_ms,hkn_enc_plane_total_ms,hkn_enc_plane_block_classify_ms,hkn_enc_class_copy_shortcut_selected,hkn_enc_plane_filter_rows_ms,hkn_enc_plane_lo_stream_ms,hkn_enc_lo_mode2_eval_ms,hkn_enc_lo_mode3_eval_ms,hkn_enc_lo_mode4_eval_ms,hkn_enc_lo_mode5_eval_ms,hkn_enc_filter_lo_mode0,hkn_enc_filter_lo_mode1,hkn_enc_filter_lo_mode2,hkn_enc_filter_lo_mode3,hkn_enc_filter_lo_mode4,hkn_enc_filter_lo_mode5,hkn_enc_lo_lz_probe_enabled,hkn_enc_lo_lz_probe_checked,hkn_enc_lo_lz_probe_pass,hkn_enc_lo_lz_probe_skip,hkn_enc_lo_lz_probe_sample_bytes,hkn_enc_lo_lz_probe_sample_lz_bytes,hkn_enc_lo_lz_probe_sample_wrapped_bytes,hkn_enc_plane_hi_stream_ms,hkn_enc_plane_stream_wrap_ms,hkn_enc_plane_route_ms,hkn_enc_plane_route_prefilter_ms,hkn_enc_plane_route_screen_candidate_ms,hkn_enc_plane_route_natural_candidate_ms,hkn_enc_plane_route_parallel,hkn_enc_plane_route_seq,hkn_enc_plane_route_parallel_tokens_sum,hkn_enc_route_nat_mode0_ms,hkn_enc_route_nat_mode1prep_ms,hkn_enc_route_nat_predpack_ms,hkn_enc_route_nat_mode1_ms,hkn_enc_route_nat_mode2_ms,hkn_enc_route_nat_mode3_ms,hkn_enc_route_nat_mode0_selected,hkn_enc_route_nat_mode1_selected,hkn_enc_route_nat_mode2_selected,hkn_enc_route_nat_mode3_selected,hkn_enc_route_nat_pred_raw,hkn_enc_route_nat_pred_rans,hkn_enc_route_nat_mode2_bias_adopt,hkn_enc_route_nat_mode2_bias_reject,hkn_enc_route_nat_mode2_lz_calls,hkn_enc_route_nat_mode2_lz_src_bytes,hkn_enc_route_nat_mode2_lz_out_bytes,hkn_enc_route_nat_mode2_lz_match_count,hkn_enc_route_nat_mode2_lz_match_bytes,hkn_enc_route_nat_mode2_lz_literal_bytes,hkn_enc_route_nat_mode2_lz_chain_steps,hkn_enc_route_nat_mode2_lz_depth_limit_hits,hkn_enc_route_nat_mode2_lz_early_maxlen_hits,hkn_enc_route_nat_mode2_lz_nice_cutoff_hits,hkn_enc_route_nat_mode2_lz_len3_reject_dist,hkn_enc_route_nat_prep_parallel,hkn_enc_route_nat_prep_seq,hkn_enc_route_nat_prep_tokens_sum,hkn_enc_route_nat_mode12_parallel,hkn_enc_route_nat_mode12_seq,hkn_enc_route_nat_mode12_tokens_sum,hkn_enc_container_pack_ms,hkn_dec_header_ms,hkn_dec_plane_total_ms,hkn_dec_ycocg_to_rgb_ms,hkn_dec_plane_dispatch_ms,hkn_dec_plane_wait_ms,hkn_dec_ycocg_dispatch_ms,hkn_dec_ycocg_kernel_ms,hkn_dec_ycocg_wait_ms,hkn_dec_ycocg_rows_sum,hkn_dec_ycocg_pixels_sum,hkn_dec_plane_try_natural_ms,hkn_dec_plane_screen_wrapper_ms,hkn_dec_plane_block_types_ms,hkn_dec_plane_filter_ids_ms,hkn_dec_plane_filter_lo_ms,hkn_dec_plane_filter_hi_ms,hkn_dec_plane_reconstruct_ms,hkn_enc_plane_y_ms,hkn_enc_plane_co_ms,hkn_enc_plane_cg_ms,hkn_dec_plane_y_ms,hkn_dec_plane_co_ms,hkn_dec_plane_cg_ms,hkn_enc_plane_parallel_3way,hkn_enc_plane_parallel_2way,hkn_enc_plane_parallel_seq,hkn_enc_plane_parallel_tokens_sum,hkn_dec_plane_parallel_3way,hkn_dec_plane_parallel_seq,hkn_dec_plane_parallel_tokens_sum,hkn_dec_ycocg_parallel,hkn_dec_ycocg_sequential,hkn_dec_ycocg_parallel_threads_sum,hkn_dec_filter_lo_mode_raw,hkn_dec_filter_lo_mode1,hkn_dec_filter_lo_mode2,hkn_dec_filter_lo_mode3,hkn_dec_filter_lo_mode4,hkn_dec_filter_lo_mode5,hkn_dec_filter_lo_mode_invalid,hkn_dec_filter_lo_fallback_zero_fill,hkn_dec_filter_lo_mode4_parallel_tiles,hkn_dec_filter_lo_mode4_sequential_tiles,hkn_dec_filter_lo_decode_rans_ms,hkn_dec_filter_lo_decode_shared_rans_ms,hkn_dec_filter_lo_tilelz_ms,hkn_dec_recon_copy_fast_rows,hkn_dec_recon_copy_slow_rows,hkn_dec_recon_tile4_fast_quads,hkn_dec_recon_tile4_slow_quads,hkn_dec_recon_residual_missing\n";
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

ResultRow benchmark_one(const EvalImage& img, const Args& args) {
    ResultRow row;
    row.image_id = img.rel_path;
    row.image_name = img.name;

    const std::string full_path = args.base_dir + "/" + img.rel_path;
    auto ppm = load_ppm(full_path);
    row.width = ppm.width;
    row.height = ppm.height;

    std::cout << "[RUN] " << img.name << " ... " << std::flush;

    std::vector<size_t> hkn_size_samples;
    std::vector<size_t> png_size_samples;
    std::vector<double> hkn_enc_samples_ms;
    std::vector<double> hkn_dec_samples_ms;
    std::vector<double> png_enc_samples_ms;
    std::vector<double> png_dec_samples_ms;
    std::vector<double> hkn_enc_rgb_to_ycocg_samples_ms;
    std::vector<double> hkn_enc_profile_samples_ms;
    std::vector<double> hkn_enc_plane_total_samples_ms;
    std::vector<double> hkn_enc_plane_block_classify_samples_ms;
    std::vector<uint64_t> hkn_enc_class_copy_shortcut_selected_samples;
    std::vector<double> hkn_enc_plane_filter_rows_samples_ms;
    std::vector<double> hkn_enc_plane_lo_stream_samples_ms;
    std::vector<double> hkn_enc_lo_mode2_eval_samples_ms;
    std::vector<double> hkn_enc_lo_mode3_eval_samples_ms;
    std::vector<double> hkn_enc_lo_mode4_eval_samples_ms;
    std::vector<double> hkn_enc_lo_mode5_eval_samples_ms;
    std::vector<uint64_t> hkn_enc_filter_lo_mode0_samples;
    std::vector<uint64_t> hkn_enc_filter_lo_mode1_samples;
    std::vector<uint64_t> hkn_enc_filter_lo_mode2_samples;
    std::vector<uint64_t> hkn_enc_filter_lo_mode3_samples;
    std::vector<uint64_t> hkn_enc_filter_lo_mode4_samples;
    std::vector<uint64_t> hkn_enc_filter_lo_mode5_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_enabled_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_checked_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_pass_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_skip_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_sample_bytes_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_sample_lz_bytes_samples;
    std::vector<uint64_t> hkn_enc_lo_lz_probe_sample_wrapped_bytes_samples;
    std::vector<double> hkn_enc_plane_hi_stream_samples_ms;
    std::vector<double> hkn_enc_plane_stream_wrap_samples_ms;
    std::vector<double> hkn_enc_plane_route_samples_ms;
    std::vector<double> hkn_enc_plane_route_prefilter_samples_ms;
    std::vector<double> hkn_enc_plane_route_screen_candidate_samples_ms;
    std::vector<double> hkn_enc_plane_route_natural_candidate_samples_ms;
    std::vector<uint64_t> hkn_enc_plane_route_parallel_samples;
    std::vector<uint64_t> hkn_enc_plane_route_seq_samples;
    std::vector<uint64_t> hkn_enc_plane_route_parallel_tokens_sum_samples;
    std::vector<double> hkn_enc_route_nat_mode0_samples_ms;
    std::vector<double> hkn_enc_route_nat_mode1prep_samples_ms;
    std::vector<double> hkn_enc_route_nat_predpack_samples_ms;
    std::vector<double> hkn_enc_route_nat_mode1_samples_ms;
    std::vector<double> hkn_enc_route_nat_mode2_samples_ms;
    std::vector<double> hkn_enc_route_nat_mode3_samples_ms;
    std::vector<uint64_t> hkn_enc_route_nat_mode0_selected_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode1_selected_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_selected_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode3_selected_samples;
    std::vector<uint64_t> hkn_enc_route_nat_pred_raw_samples;
    std::vector<uint64_t> hkn_enc_route_nat_pred_rans_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_bias_adopt_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_bias_reject_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_calls_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_src_bytes_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_out_bytes_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_match_count_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_match_bytes_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_literal_bytes_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_chain_steps_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_depth_limit_hits_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_early_maxlen_hits_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_nice_cutoff_hits_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode2_lz_len3_reject_dist_samples;
    std::vector<uint64_t> hkn_enc_route_nat_prep_parallel_samples;
    std::vector<uint64_t> hkn_enc_route_nat_prep_seq_samples;
    std::vector<uint64_t> hkn_enc_route_nat_prep_tokens_sum_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode12_parallel_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode12_seq_samples;
    std::vector<uint64_t> hkn_enc_route_nat_mode12_tokens_sum_samples;
    std::vector<double> hkn_enc_container_pack_samples_ms;
    std::vector<double> hkn_enc_plane_y_samples_ms;
    std::vector<double> hkn_enc_plane_co_samples_ms;
    std::vector<double> hkn_enc_plane_cg_samples_ms;
    std::vector<double> hkn_dec_header_samples_ms;
    std::vector<double> hkn_dec_plane_total_samples_ms;
    std::vector<double> hkn_dec_ycocg_to_rgb_samples_ms;
    std::vector<double> hkn_dec_plane_dispatch_samples_ms;
    std::vector<double> hkn_dec_plane_wait_samples_ms;
    std::vector<double> hkn_dec_ycocg_dispatch_samples_ms;
    std::vector<double> hkn_dec_ycocg_kernel_samples_ms;
    std::vector<double> hkn_dec_ycocg_wait_samples_ms;
    std::vector<double> hkn_dec_plane_try_natural_samples_ms;
    std::vector<double> hkn_dec_plane_screen_wrapper_samples_ms;
    std::vector<double> hkn_dec_plane_block_types_samples_ms;
    std::vector<double> hkn_dec_plane_filter_ids_samples_ms;
    std::vector<double> hkn_dec_plane_filter_lo_samples_ms;
    std::vector<double> hkn_dec_plane_filter_hi_samples_ms;
    std::vector<double> hkn_dec_plane_reconstruct_samples_ms;
    std::vector<double> hkn_dec_plane_y_samples_ms;
    std::vector<double> hkn_dec_plane_co_samples_ms;
    std::vector<double> hkn_dec_plane_cg_samples_ms;
    std::vector<uint64_t> selected_samples;
    std::vector<uint64_t> candidate_samples;
    std::vector<uint64_t> gain_samples;
    std::vector<uint64_t> loss_samples;
    std::vector<uint64_t> enc_parallel_3way_samples;
    std::vector<uint64_t> enc_parallel_2way_samples;
    std::vector<uint64_t> enc_parallel_seq_samples;
    std::vector<uint64_t> enc_parallel_tokens_sum_samples;
    std::vector<uint64_t> dec_parallel_3way_samples;
    std::vector<uint64_t> dec_parallel_seq_samples;
    std::vector<uint64_t> dec_parallel_tokens_sum_samples;
    std::vector<uint64_t> dec_ycocg_parallel_samples;
    std::vector<uint64_t> dec_ycocg_seq_samples;
    std::vector<uint64_t> dec_ycocg_threads_sum_samples;
    std::vector<uint64_t> dec_ycocg_rows_sum_samples;
    std::vector<uint64_t> dec_ycocg_pixels_sum_samples;
    std::vector<uint64_t> dec_filter_lo_mode_raw_samples;
    std::vector<uint64_t> dec_filter_lo_mode1_samples;
    std::vector<uint64_t> dec_filter_lo_mode2_samples;
    std::vector<uint64_t> dec_filter_lo_mode3_samples;
    std::vector<uint64_t> dec_filter_lo_mode4_samples;
    std::vector<uint64_t> dec_filter_lo_mode5_samples;
    std::vector<uint64_t> dec_filter_lo_mode_invalid_samples;
    std::vector<uint64_t> dec_filter_lo_fallback_zero_fill_samples;
    std::vector<uint64_t> dec_filter_lo_mode4_parallel_tiles_samples;
    std::vector<uint64_t> dec_filter_lo_mode4_seq_tiles_samples;
    std::vector<double> dec_filter_lo_decode_rans_samples_ms;
    std::vector<double> dec_filter_lo_decode_shared_rans_samples_ms;
    std::vector<double> dec_filter_lo_tilelz_samples_ms;
    std::vector<uint64_t> dec_recon_copy_fast_rows_samples;
    std::vector<uint64_t> dec_recon_copy_slow_rows_samples;
    std::vector<uint64_t> dec_recon_tile4_fast_quads_samples;
    std::vector<uint64_t> dec_recon_tile4_slow_quads_samples;
    std::vector<uint64_t> dec_recon_residual_missing_samples;

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

    row.hkn_bytes = median_value(hkn_size_samples);
    row.png_bytes = median_value(png_size_samples);
    row.hkn_enc_ms = median_value(hkn_enc_samples_ms);
    row.hkn_dec_ms = median_value(hkn_dec_samples_ms);
    row.png_enc_ms = median_value(png_enc_samples_ms);
    row.png_dec_ms = median_value(png_dec_samples_ms);
    row.hkn_enc_rgb_to_ycocg_ms = median_value(hkn_enc_rgb_to_ycocg_samples_ms);
    row.hkn_enc_profile_ms = median_value(hkn_enc_profile_samples_ms);
    row.hkn_enc_plane_total_ms = median_value(hkn_enc_plane_total_samples_ms);
    row.hkn_enc_plane_block_classify_ms = median_value(hkn_enc_plane_block_classify_samples_ms);
    row.hkn_enc_class_copy_shortcut_selected =
        median_value(hkn_enc_class_copy_shortcut_selected_samples);
    row.hkn_enc_plane_filter_rows_ms = median_value(hkn_enc_plane_filter_rows_samples_ms);
    row.hkn_enc_plane_lo_stream_ms = median_value(hkn_enc_plane_lo_stream_samples_ms);
    row.hkn_enc_lo_mode2_eval_ms = median_value(hkn_enc_lo_mode2_eval_samples_ms);
    row.hkn_enc_lo_mode3_eval_ms = median_value(hkn_enc_lo_mode3_eval_samples_ms);
    row.hkn_enc_lo_mode4_eval_ms = median_value(hkn_enc_lo_mode4_eval_samples_ms);
    row.hkn_enc_lo_mode5_eval_ms = median_value(hkn_enc_lo_mode5_eval_samples_ms);
    row.hkn_enc_filter_lo_mode0 = median_value(hkn_enc_filter_lo_mode0_samples);
    row.hkn_enc_filter_lo_mode1 = median_value(hkn_enc_filter_lo_mode1_samples);
    row.hkn_enc_filter_lo_mode2 = median_value(hkn_enc_filter_lo_mode2_samples);
    row.hkn_enc_filter_lo_mode3 = median_value(hkn_enc_filter_lo_mode3_samples);
    row.hkn_enc_filter_lo_mode4 = median_value(hkn_enc_filter_lo_mode4_samples);
    row.hkn_enc_filter_lo_mode5 = median_value(hkn_enc_filter_lo_mode5_samples);
    row.hkn_enc_lo_lz_probe_enabled = median_value(hkn_enc_lo_lz_probe_enabled_samples);
    row.hkn_enc_lo_lz_probe_checked = median_value(hkn_enc_lo_lz_probe_checked_samples);
    row.hkn_enc_lo_lz_probe_pass = median_value(hkn_enc_lo_lz_probe_pass_samples);
    row.hkn_enc_lo_lz_probe_skip = median_value(hkn_enc_lo_lz_probe_skip_samples);
    row.hkn_enc_lo_lz_probe_sample_bytes = median_value(hkn_enc_lo_lz_probe_sample_bytes_samples);
    row.hkn_enc_lo_lz_probe_sample_lz_bytes = median_value(hkn_enc_lo_lz_probe_sample_lz_bytes_samples);
    row.hkn_enc_lo_lz_probe_sample_wrapped_bytes = median_value(
        hkn_enc_lo_lz_probe_sample_wrapped_bytes_samples
    );
    row.hkn_enc_plane_hi_stream_ms = median_value(hkn_enc_plane_hi_stream_samples_ms);
    row.hkn_enc_plane_stream_wrap_ms = median_value(hkn_enc_plane_stream_wrap_samples_ms);
    row.hkn_enc_plane_route_ms = median_value(hkn_enc_plane_route_samples_ms);
    row.hkn_enc_plane_route_prefilter_ms = median_value(hkn_enc_plane_route_prefilter_samples_ms);
    row.hkn_enc_plane_route_screen_candidate_ms = median_value(hkn_enc_plane_route_screen_candidate_samples_ms);
    row.hkn_enc_plane_route_natural_candidate_ms = median_value(hkn_enc_plane_route_natural_candidate_samples_ms);
    row.hkn_enc_plane_route_parallel = median_value(hkn_enc_plane_route_parallel_samples);
    row.hkn_enc_plane_route_seq = median_value(hkn_enc_plane_route_seq_samples);
    row.hkn_enc_plane_route_parallel_tokens_sum = median_value(hkn_enc_plane_route_parallel_tokens_sum_samples);
    row.hkn_enc_route_nat_mode0_ms = median_value(hkn_enc_route_nat_mode0_samples_ms);
    row.hkn_enc_route_nat_mode1prep_ms = median_value(hkn_enc_route_nat_mode1prep_samples_ms);
    row.hkn_enc_route_nat_predpack_ms = median_value(hkn_enc_route_nat_predpack_samples_ms);
    row.hkn_enc_route_nat_mode1_ms = median_value(hkn_enc_route_nat_mode1_samples_ms);
    row.hkn_enc_route_nat_mode2_ms = median_value(hkn_enc_route_nat_mode2_samples_ms);
    row.hkn_enc_route_nat_mode3_ms = median_value(hkn_enc_route_nat_mode3_samples_ms);
    row.hkn_enc_route_nat_mode0_selected = median_value(hkn_enc_route_nat_mode0_selected_samples);
    row.hkn_enc_route_nat_mode1_selected = median_value(hkn_enc_route_nat_mode1_selected_samples);
    row.hkn_enc_route_nat_mode2_selected = median_value(hkn_enc_route_nat_mode2_selected_samples);
    row.hkn_enc_route_nat_mode3_selected = median_value(hkn_enc_route_nat_mode3_selected_samples);
    row.hkn_enc_route_nat_pred_raw = median_value(hkn_enc_route_nat_pred_raw_samples);
    row.hkn_enc_route_nat_pred_rans = median_value(hkn_enc_route_nat_pred_rans_samples);
    row.hkn_enc_route_nat_mode2_bias_adopt = median_value(hkn_enc_route_nat_mode2_bias_adopt_samples);
    row.hkn_enc_route_nat_mode2_bias_reject = median_value(hkn_enc_route_nat_mode2_bias_reject_samples);
    row.hkn_enc_route_nat_mode2_lz_calls = median_value(hkn_enc_route_nat_mode2_lz_calls_samples);
    row.hkn_enc_route_nat_mode2_lz_src_bytes = median_value(hkn_enc_route_nat_mode2_lz_src_bytes_samples);
    row.hkn_enc_route_nat_mode2_lz_out_bytes = median_value(hkn_enc_route_nat_mode2_lz_out_bytes_samples);
    row.hkn_enc_route_nat_mode2_lz_match_count = median_value(hkn_enc_route_nat_mode2_lz_match_count_samples);
    row.hkn_enc_route_nat_mode2_lz_match_bytes = median_value(hkn_enc_route_nat_mode2_lz_match_bytes_samples);
    row.hkn_enc_route_nat_mode2_lz_literal_bytes = median_value(hkn_enc_route_nat_mode2_lz_literal_bytes_samples);
    row.hkn_enc_route_nat_mode2_lz_chain_steps = median_value(hkn_enc_route_nat_mode2_lz_chain_steps_samples);
    row.hkn_enc_route_nat_mode2_lz_depth_limit_hits = median_value(hkn_enc_route_nat_mode2_lz_depth_limit_hits_samples);
    row.hkn_enc_route_nat_mode2_lz_early_maxlen_hits = median_value(hkn_enc_route_nat_mode2_lz_early_maxlen_hits_samples);
    row.hkn_enc_route_nat_mode2_lz_nice_cutoff_hits = median_value(hkn_enc_route_nat_mode2_lz_nice_cutoff_hits_samples);
    row.hkn_enc_route_nat_mode2_lz_len3_reject_dist = median_value(hkn_enc_route_nat_mode2_lz_len3_reject_dist_samples);
    row.hkn_enc_route_nat_prep_parallel = median_value(hkn_enc_route_nat_prep_parallel_samples);
    row.hkn_enc_route_nat_prep_seq = median_value(hkn_enc_route_nat_prep_seq_samples);
    row.hkn_enc_route_nat_prep_tokens_sum = median_value(hkn_enc_route_nat_prep_tokens_sum_samples);
    row.hkn_enc_route_nat_mode12_parallel = median_value(hkn_enc_route_nat_mode12_parallel_samples);
    row.hkn_enc_route_nat_mode12_seq = median_value(hkn_enc_route_nat_mode12_seq_samples);
    row.hkn_enc_route_nat_mode12_tokens_sum = median_value(hkn_enc_route_nat_mode12_tokens_sum_samples);
    row.hkn_enc_container_pack_ms = median_value(hkn_enc_container_pack_samples_ms);
    row.hkn_enc_plane_y_ms = median_value(hkn_enc_plane_y_samples_ms);
    row.hkn_enc_plane_co_ms = median_value(hkn_enc_plane_co_samples_ms);
    row.hkn_enc_plane_cg_ms = median_value(hkn_enc_plane_cg_samples_ms);
    row.hkn_dec_header_ms = median_value(hkn_dec_header_samples_ms);
    row.hkn_dec_plane_total_ms = median_value(hkn_dec_plane_total_samples_ms);
    row.hkn_dec_ycocg_to_rgb_ms = median_value(hkn_dec_ycocg_to_rgb_samples_ms);
    row.hkn_dec_plane_dispatch_ms = median_value(hkn_dec_plane_dispatch_samples_ms);
    row.hkn_dec_plane_wait_ms = median_value(hkn_dec_plane_wait_samples_ms);
    row.hkn_dec_ycocg_dispatch_ms = median_value(hkn_dec_ycocg_dispatch_samples_ms);
    row.hkn_dec_ycocg_kernel_ms = median_value(hkn_dec_ycocg_kernel_samples_ms);
    row.hkn_dec_ycocg_wait_ms = median_value(hkn_dec_ycocg_wait_samples_ms);
    row.hkn_dec_plane_try_natural_ms = median_value(hkn_dec_plane_try_natural_samples_ms);
    row.hkn_dec_plane_screen_wrapper_ms = median_value(hkn_dec_plane_screen_wrapper_samples_ms);
    row.hkn_dec_plane_block_types_ms = median_value(hkn_dec_plane_block_types_samples_ms);
    row.hkn_dec_plane_filter_ids_ms = median_value(hkn_dec_plane_filter_ids_samples_ms);
    row.hkn_dec_plane_filter_lo_ms = median_value(hkn_dec_plane_filter_lo_samples_ms);
    row.hkn_dec_plane_filter_hi_ms = median_value(hkn_dec_plane_filter_hi_samples_ms);
    row.hkn_dec_plane_reconstruct_ms = median_value(hkn_dec_plane_reconstruct_samples_ms);
    row.hkn_dec_plane_y_ms = median_value(hkn_dec_plane_y_samples_ms);
    row.hkn_dec_plane_co_ms = median_value(hkn_dec_plane_co_samples_ms);
    row.hkn_dec_plane_cg_ms = median_value(hkn_dec_plane_cg_samples_ms);
    row.dec_ms = row.hkn_dec_ms;
    row.natural_row_selected = median_value(selected_samples);
    row.natural_row_candidates = median_value(candidate_samples);
    row.gain_bytes = median_value(gain_samples);
    row.loss_bytes = median_value(loss_samples);
    row.hkn_enc_plane_parallel_3way = median_value(enc_parallel_3way_samples);
    row.hkn_enc_plane_parallel_2way = median_value(enc_parallel_2way_samples);
    row.hkn_enc_plane_parallel_seq = median_value(enc_parallel_seq_samples);
    row.hkn_enc_plane_parallel_tokens_sum = median_value(enc_parallel_tokens_sum_samples);
    row.hkn_dec_plane_parallel_3way = median_value(dec_parallel_3way_samples);
    row.hkn_dec_plane_parallel_seq = median_value(dec_parallel_seq_samples);
    row.hkn_dec_plane_parallel_tokens_sum = median_value(dec_parallel_tokens_sum_samples);
    row.hkn_dec_ycocg_parallel = median_value(dec_ycocg_parallel_samples);
    row.hkn_dec_ycocg_sequential = median_value(dec_ycocg_seq_samples);
    row.hkn_dec_ycocg_parallel_threads_sum = median_value(dec_ycocg_threads_sum_samples);
    row.hkn_dec_ycocg_rows_sum = median_value(dec_ycocg_rows_sum_samples);
    row.hkn_dec_ycocg_pixels_sum = median_value(dec_ycocg_pixels_sum_samples);
    row.hkn_dec_filter_lo_mode_raw = median_value(dec_filter_lo_mode_raw_samples);
    row.hkn_dec_filter_lo_mode1 = median_value(dec_filter_lo_mode1_samples);
    row.hkn_dec_filter_lo_mode2 = median_value(dec_filter_lo_mode2_samples);
    row.hkn_dec_filter_lo_mode3 = median_value(dec_filter_lo_mode3_samples);
    row.hkn_dec_filter_lo_mode4 = median_value(dec_filter_lo_mode4_samples);
    row.hkn_dec_filter_lo_mode5 = median_value(dec_filter_lo_mode5_samples);
    row.hkn_dec_filter_lo_mode_invalid = median_value(dec_filter_lo_mode_invalid_samples);
    row.hkn_dec_filter_lo_fallback_zero_fill = median_value(dec_filter_lo_fallback_zero_fill_samples);
    row.hkn_dec_filter_lo_mode4_parallel_tiles = median_value(dec_filter_lo_mode4_parallel_tiles_samples);
    row.hkn_dec_filter_lo_mode4_sequential_tiles = median_value(dec_filter_lo_mode4_seq_tiles_samples);
    row.hkn_dec_filter_lo_decode_rans_ms = median_value(dec_filter_lo_decode_rans_samples_ms);
    row.hkn_dec_filter_lo_decode_shared_rans_ms = median_value(dec_filter_lo_decode_shared_rans_samples_ms);
    row.hkn_dec_filter_lo_tilelz_ms = median_value(dec_filter_lo_tilelz_samples_ms);
    row.hkn_dec_recon_copy_fast_rows = median_value(dec_recon_copy_fast_rows_samples);
    row.hkn_dec_recon_copy_slow_rows = median_value(dec_recon_copy_slow_rows_samples);
    row.hkn_dec_recon_tile4_fast_quads = median_value(dec_recon_tile4_fast_quads_samples);
    row.hkn_dec_recon_tile4_slow_quads = median_value(dec_recon_tile4_slow_quads_samples);
    row.hkn_dec_recon_residual_missing = median_value(dec_recon_residual_missing_samples);

    auto ms_to_images_per_s = [](double ms) -> double {
        return (ms > 0.0) ? (1000.0 / ms) : 0.0;
    };
    row.hkn_enc_images_per_s = ms_to_images_per_s(row.hkn_enc_ms);
    row.hkn_dec_images_per_s = ms_to_images_per_s(row.hkn_dec_ms);
    row.png_enc_images_per_s = ms_to_images_per_s(row.png_enc_ms);
    row.png_dec_images_per_s = ms_to_images_per_s(row.png_dec_ms);
    const double row_enc_cpu_sum =
        row.hkn_enc_rgb_to_ycocg_ms +
        row.hkn_enc_profile_ms +
        row.hkn_enc_plane_total_ms +
        row.hkn_enc_container_pack_ms;
    const double row_dec_cpu_sum =
        row.hkn_dec_header_ms +
        row.hkn_dec_plane_total_ms +
        row.hkn_dec_ycocg_to_rgb_ms;
    row.hkn_enc_cpu_over_wall = (row.hkn_enc_ms > 0.0) ? (row_enc_cpu_sum / row.hkn_enc_ms) : 0.0;
    row.hkn_dec_cpu_over_wall = (row.hkn_dec_ms > 0.0) ? (row_dec_cpu_sum / row.hkn_dec_ms) : 0.0;

    if (row.hkn_bytes > 0) {
        row.png_over_hkn = (double)row.png_bytes / (double)row.hkn_bytes;
    }
    if (row.natural_row_candidates > 0) {
        row.natural_row_selected_rate =
            100.0 * (double)row.natural_row_selected / (double)row.natural_row_candidates;
    }

    std::cout << "done\n";
    return row;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Args args = parse_args(argc, argv);

        std::cout << "=== Phase 9w Fixed 6-image A/B Evaluation ===\n";
        std::cout << "base_dir: " << args.base_dir << "\n";
        std::cout << "runs: " << args.runs << " (warmup=" << args.warmup << ")\n";
        std::cout << "preset: " << GrayscaleEncoder::lossless_preset_name(args.preset) << "\n";
        if (!args.baseline_csv.empty()) {
            std::cout << "baseline: " << args.baseline_csv << "\n";
        }
        std::cout << "\n";

        std::vector<ResultRow> rows;
        rows.reserve(kFixedEvalSet.size());

        for (const auto& img : kFixedEvalSet) {
            rows.push_back(benchmark_one(img, args));
        }

        write_results_csv(args.out_csv, rows);

        std::cout << "\n=== Per-image Metrics (fixed 6) ===\n";
        std::cout << "Image       size_bytes(HKN/PNG)    Enc(ms HKN/PNG)   Dec(ms HKN/PNG)   natural_row_selected   gain_bytes  loss_bytes  PNG/HKN\n";
        for (const auto& r : rows) {
            std::ostringstream sel;
            sel << r.natural_row_selected << "/" << r.natural_row_candidates
                << " (" << std::fixed << std::setprecision(1)
                << r.natural_row_selected_rate << "%)";
            std::ostringstream enc_pair;
            enc_pair << std::fixed << std::setprecision(3) << r.hkn_enc_ms << "/" << r.png_enc_ms;
            std::ostringstream dec_pair;
            dec_pair << std::fixed << std::setprecision(3) << r.hkn_dec_ms << "/" << r.png_dec_ms;
            std::cout << std::left << std::setw(10) << r.image_name
                      << std::right << std::setw(12) << r.hkn_bytes << "/"
                      << std::left << std::setw(12) << r.png_bytes
                      << std::right << std::setw(19) << enc_pair.str()
                      << std::setw(19) << dec_pair.str()
                      << std::setw(23) << sel.str()
                      << std::setw(12) << r.gain_bytes
                      << std::setw(11) << r.loss_bytes
                      << std::setw(9) << std::fixed << std::setprecision(3) << r.png_over_hkn
                      << "\n";
        }

        std::vector<double> ratios;
        ratios.reserve(rows.size());
        for (const auto& r : rows) ratios.push_back(r.png_over_hkn);
        double median_ratio = median_value(ratios);
        std::cout << "\nmedian(PNG_bytes/HKN_bytes): "
                  << std::fixed << std::setprecision(4) << median_ratio << "\n";

        std::vector<double> hkn_enc, hkn_dec, png_enc, png_dec;
        hkn_enc.reserve(rows.size());
        hkn_dec.reserve(rows.size());
        png_enc.reserve(rows.size());
        png_dec.reserve(rows.size());
        for (const auto& r : rows) {
            hkn_enc.push_back(r.hkn_enc_ms);
            hkn_dec.push_back(r.hkn_dec_ms);
            png_enc.push_back(r.png_enc_ms);
            png_dec.push_back(r.png_dec_ms);
        }
        double med_hkn_enc = median_value(hkn_enc);
        double med_hkn_dec = median_value(hkn_dec);
        double med_png_enc = median_value(png_enc);
        double med_png_dec = median_value(png_dec);
        std::cout << "median Enc(ms) HKN/PNG: "
                  << std::fixed << std::setprecision(3)
                  << med_hkn_enc << "/" << med_png_enc;
        if (med_png_enc > 0.0) {
            std::cout << " (HKN/PNG=" << std::setprecision(3) << (med_hkn_enc / med_png_enc) << ")";
        }
        std::cout << "\n";
        std::cout << "median Dec(ms) HKN/PNG: "
                  << std::fixed << std::setprecision(3)
                  << med_hkn_dec << "/" << med_png_dec;
        if (med_png_dec > 0.0) {
            std::cout << " (HKN/PNG=" << std::setprecision(3) << (med_hkn_dec / med_png_dec) << ")";
        }
        std::cout << "\n";

        auto ms_to_images_per_s = [](double ms) -> double {
            return (ms > 0.0) ? (1000.0 / ms) : 0.0;
        };
        const double med_hkn_enc_ips = ms_to_images_per_s(med_hkn_enc);
        const double med_hkn_dec_ips = ms_to_images_per_s(med_hkn_dec);
        const double med_png_enc_ips = ms_to_images_per_s(med_png_enc);
        const double med_png_dec_ips = ms_to_images_per_s(med_png_dec);
        std::vector<double> v_enc_cpu_over_wall, v_dec_cpu_over_wall;
        v_enc_cpu_over_wall.reserve(rows.size());
        v_dec_cpu_over_wall.reserve(rows.size());
        for (const auto& r : rows) {
            v_enc_cpu_over_wall.push_back(r.hkn_enc_cpu_over_wall);
            v_dec_cpu_over_wall.push_back(r.hkn_dec_cpu_over_wall);
        }
        const double med_hkn_enc_cpu_over_wall = median_value(v_enc_cpu_over_wall);
        const double med_hkn_dec_cpu_over_wall = median_value(v_dec_cpu_over_wall);
        std::cout << "\n=== Batch Indicators (median per image) ===\n";
        std::cout << "images/s Enc HKN/PNG: "
                  << std::fixed << std::setprecision(3)
                  << med_hkn_enc_ips << "/" << med_png_enc_ips;
        if (med_png_enc_ips > 0.0) {
            std::cout << " (HKN/PNG=" << std::setprecision(3)
                      << (med_hkn_enc_ips / med_png_enc_ips) << ")";
        }
        std::cout << "\n";
        std::cout << "images/s Dec HKN/PNG: "
                  << std::fixed << std::setprecision(3)
                  << med_hkn_dec_ips << "/" << med_png_dec_ips;
        if (med_png_dec_ips > 0.0) {
            std::cout << " (HKN/PNG=" << std::setprecision(3)
                      << (med_hkn_dec_ips / med_png_dec_ips) << ")";
        }
        std::cout << "\n";
        std::cout << "cpu/wall Enc(HKN): "
                  << std::fixed << std::setprecision(3)
                  << med_hkn_enc_cpu_over_wall << "\n";
        std::cout << "cpu/wall Dec(HKN): "
                  << std::fixed << std::setprecision(3)
                  << med_hkn_dec_cpu_over_wall << "\n";

        std::vector<double> v_enc_rgb, v_enc_cls, v_enc_plane, v_enc_blk, v_enc_rows, v_enc_lo, v_enc_lo_m2, v_enc_lo_m3, v_enc_lo_m4, v_enc_lo_m5, v_enc_hi, v_enc_wrap, v_enc_route, v_enc_route_pref, v_enc_route_screen, v_enc_route_nat, v_enc_route_nat_m0, v_enc_route_nat_m1prep, v_enc_route_nat_predpack, v_enc_route_nat_m1, v_enc_route_nat_m2, v_enc_route_nat_m3, v_enc_pack;
        std::vector<double> v_dec_hdr, v_dec_plane, v_dec_ycocg, v_dec_plane_dispatch, v_dec_plane_wait, v_dec_ycocg_dispatch, v_dec_ycocg_kernel, v_dec_ycocg_wait, v_dec_nat, v_dec_screen, v_dec_bt, v_dec_fid, v_dec_lo, v_dec_hi, v_dec_recon;
        std::vector<double> v_enc_py, v_enc_pco, v_enc_pcg;
        std::vector<double> v_dec_py, v_dec_pco, v_dec_pcg;
        std::vector<double> v_dec_lo_rans, v_dec_lo_shared_rans, v_dec_lo_lz;
        std::vector<uint64_t> v_enc_lo_sel0, v_enc_lo_sel1, v_enc_lo_sel2, v_enc_lo_sel3, v_enc_lo_sel4, v_enc_lo_sel5;
        std::vector<uint64_t> v_enc_lo_probe_enabled, v_enc_lo_probe_checked, v_enc_lo_probe_pass, v_enc_lo_probe_skip, v_enc_lo_probe_sample, v_enc_lo_probe_sample_lz, v_enc_lo_probe_sample_wrapped;
        std::vector<uint64_t> v_enc_p3, v_enc_p2, v_enc_ps, v_enc_ptok, v_enc_route_par, v_enc_route_seq, v_enc_route_tok, v_nat_mode0_sel, v_nat_mode1_sel, v_nat_mode2_sel, v_nat_mode3_sel, v_nat_pred_raw, v_nat_pred_rans, v_nat_bias_adopt, v_nat_bias_reject, v_nat_prep_par, v_nat_prep_seq, v_nat_prep_tok, v_nat_mode12_par, v_nat_mode12_seq, v_nat_mode12_tok;
        std::vector<uint64_t> v_nat_m2_lz_calls, v_nat_m2_lz_src, v_nat_m2_lz_out, v_nat_m2_lz_match_count, v_nat_m2_lz_match_bytes, v_nat_m2_lz_literal_bytes, v_nat_m2_lz_chain_steps, v_nat_m2_lz_depth_hits, v_nat_m2_lz_maxlen_hits, v_nat_m2_lz_nice_hits, v_nat_m2_lz_len3_reject;
        std::vector<uint64_t> v_dec_p3, v_dec_ps, v_dec_ptok, v_dec_rgb_p, v_dec_rgb_s, v_dec_rgb_thr, v_dec_rgb_rows, v_dec_rgb_pixels;
        std::vector<uint64_t> v_lo_raw, v_lo_m1, v_lo_m2, v_lo_m3, v_lo_m4, v_lo_m5, v_lo_inv, v_lo_fb;
        std::vector<uint64_t> v_lo_m4_par, v_lo_m4_seq;
        std::vector<uint64_t> v_rc_copy_fast, v_rc_copy_slow, v_rc_t4_fast, v_rc_t4_slow, v_rc_res_miss;
        v_enc_rgb.reserve(rows.size());
        v_enc_cls.reserve(rows.size());
        v_enc_plane.reserve(rows.size());
        v_enc_blk.reserve(rows.size());
        v_enc_rows.reserve(rows.size());
        v_enc_lo.reserve(rows.size());
        v_enc_lo_m2.reserve(rows.size());
        v_enc_lo_m3.reserve(rows.size());
        v_enc_lo_m4.reserve(rows.size());
        v_enc_lo_m5.reserve(rows.size());
        v_enc_hi.reserve(rows.size());
        v_enc_wrap.reserve(rows.size());
        v_enc_route.reserve(rows.size());
        v_enc_route_pref.reserve(rows.size());
        v_enc_route_screen.reserve(rows.size());
        v_enc_route_nat.reserve(rows.size());
        v_enc_route_nat_m0.reserve(rows.size());
        v_enc_route_nat_m1prep.reserve(rows.size());
        v_enc_route_nat_predpack.reserve(rows.size());
        v_enc_route_nat_m1.reserve(rows.size());
        v_enc_route_nat_m2.reserve(rows.size());
        v_enc_pack.reserve(rows.size());
        v_dec_hdr.reserve(rows.size());
        v_dec_plane.reserve(rows.size());
        v_dec_ycocg.reserve(rows.size());
        v_dec_plane_dispatch.reserve(rows.size());
        v_dec_plane_wait.reserve(rows.size());
        v_dec_ycocg_dispatch.reserve(rows.size());
        v_dec_ycocg_kernel.reserve(rows.size());
        v_dec_ycocg_wait.reserve(rows.size());
        v_dec_nat.reserve(rows.size());
        v_dec_screen.reserve(rows.size());
        v_dec_bt.reserve(rows.size());
        v_dec_fid.reserve(rows.size());
        v_dec_lo.reserve(rows.size());
        v_dec_hi.reserve(rows.size());
        v_dec_recon.reserve(rows.size());
        v_enc_py.reserve(rows.size());
        v_enc_pco.reserve(rows.size());
        v_enc_pcg.reserve(rows.size());
        v_dec_py.reserve(rows.size());
        v_dec_pco.reserve(rows.size());
        v_dec_pcg.reserve(rows.size());
        v_dec_lo_rans.reserve(rows.size());
        v_dec_lo_shared_rans.reserve(rows.size());
        v_dec_lo_lz.reserve(rows.size());
        v_enc_lo_sel0.reserve(rows.size());
        v_enc_lo_sel1.reserve(rows.size());
        v_enc_lo_sel2.reserve(rows.size());
        v_enc_lo_sel3.reserve(rows.size());
        v_enc_lo_sel4.reserve(rows.size());
        v_enc_lo_sel5.reserve(rows.size());
        v_enc_lo_probe_enabled.reserve(rows.size());
        v_enc_lo_probe_checked.reserve(rows.size());
        v_enc_lo_probe_pass.reserve(rows.size());
        v_enc_lo_probe_skip.reserve(rows.size());
        v_enc_lo_probe_sample.reserve(rows.size());
        v_enc_lo_probe_sample_lz.reserve(rows.size());
        v_enc_lo_probe_sample_wrapped.reserve(rows.size());
        v_enc_p3.reserve(rows.size());
        v_enc_p2.reserve(rows.size());
        v_enc_ps.reserve(rows.size());
        v_enc_ptok.reserve(rows.size());
        v_enc_route_par.reserve(rows.size());
        v_enc_route_seq.reserve(rows.size());
        v_enc_route_tok.reserve(rows.size());
        v_nat_mode0_sel.reserve(rows.size());
        v_nat_mode1_sel.reserve(rows.size());
        v_nat_mode2_sel.reserve(rows.size());
        v_nat_mode3_sel.reserve(rows.size());
        v_nat_pred_raw.reserve(rows.size());
        v_nat_pred_rans.reserve(rows.size());
        v_nat_bias_adopt.reserve(rows.size());
        v_nat_bias_reject.reserve(rows.size());
        v_nat_prep_par.reserve(rows.size());
        v_nat_prep_seq.reserve(rows.size());
        v_nat_prep_tok.reserve(rows.size());
        v_nat_mode12_par.reserve(rows.size());
        v_nat_mode12_seq.reserve(rows.size());
        v_nat_mode12_tok.reserve(rows.size());
        v_nat_m2_lz_calls.reserve(rows.size());
        v_nat_m2_lz_src.reserve(rows.size());
        v_nat_m2_lz_out.reserve(rows.size());
        v_nat_m2_lz_match_count.reserve(rows.size());
        v_nat_m2_lz_match_bytes.reserve(rows.size());
        v_nat_m2_lz_literal_bytes.reserve(rows.size());
        v_nat_m2_lz_chain_steps.reserve(rows.size());
        v_nat_m2_lz_depth_hits.reserve(rows.size());
        v_nat_m2_lz_maxlen_hits.reserve(rows.size());
        v_nat_m2_lz_nice_hits.reserve(rows.size());
        v_nat_m2_lz_len3_reject.reserve(rows.size());
        v_dec_p3.reserve(rows.size());
        v_dec_ps.reserve(rows.size());
        v_dec_ptok.reserve(rows.size());
        v_dec_rgb_p.reserve(rows.size());
        v_dec_rgb_s.reserve(rows.size());
        v_dec_rgb_thr.reserve(rows.size());
        v_dec_rgb_rows.reserve(rows.size());
        v_dec_rgb_pixels.reserve(rows.size());
        v_lo_raw.reserve(rows.size());
        v_lo_m1.reserve(rows.size());
        v_lo_m2.reserve(rows.size());
        v_lo_m3.reserve(rows.size());
        v_lo_m4.reserve(rows.size());
        v_lo_m5.reserve(rows.size());
        v_lo_inv.reserve(rows.size());
        v_lo_fb.reserve(rows.size());
        v_lo_m4_par.reserve(rows.size());
        v_lo_m4_seq.reserve(rows.size());
        v_rc_copy_fast.reserve(rows.size());
        v_rc_copy_slow.reserve(rows.size());
        v_rc_t4_fast.reserve(rows.size());
        v_rc_t4_slow.reserve(rows.size());
        v_rc_res_miss.reserve(rows.size());
        for (const auto& r : rows) {
            v_enc_rgb.push_back(r.hkn_enc_rgb_to_ycocg_ms);
            v_enc_cls.push_back(r.hkn_enc_profile_ms);
            v_enc_plane.push_back(r.hkn_enc_plane_total_ms);
            v_enc_blk.push_back(r.hkn_enc_plane_block_classify_ms);
            v_enc_rows.push_back(r.hkn_enc_plane_filter_rows_ms);
            v_enc_lo.push_back(r.hkn_enc_plane_lo_stream_ms);
            v_enc_lo_m2.push_back(r.hkn_enc_lo_mode2_eval_ms);
            v_enc_lo_m3.push_back(r.hkn_enc_lo_mode3_eval_ms);
            v_enc_lo_m4.push_back(r.hkn_enc_lo_mode4_eval_ms);
            v_enc_lo_m5.push_back(r.hkn_enc_lo_mode5_eval_ms);
            v_enc_hi.push_back(r.hkn_enc_plane_hi_stream_ms);
            v_enc_wrap.push_back(r.hkn_enc_plane_stream_wrap_ms);
            v_enc_route.push_back(r.hkn_enc_plane_route_ms);
            v_enc_route_pref.push_back(r.hkn_enc_plane_route_prefilter_ms);
            v_enc_route_screen.push_back(r.hkn_enc_plane_route_screen_candidate_ms);
            v_enc_route_nat.push_back(r.hkn_enc_plane_route_natural_candidate_ms);
            v_enc_route_nat_m0.push_back(r.hkn_enc_route_nat_mode0_ms);
            v_enc_route_nat_m1prep.push_back(r.hkn_enc_route_nat_mode1prep_ms);
            v_enc_route_nat_predpack.push_back(r.hkn_enc_route_nat_predpack_ms);
            v_enc_route_nat_m1.push_back(r.hkn_enc_route_nat_mode1_ms);
            v_enc_route_nat_m2.push_back(r.hkn_enc_route_nat_mode2_ms);
            v_enc_route_nat_m3.push_back(r.hkn_enc_route_nat_mode3_ms);
            v_enc_pack.push_back(r.hkn_enc_container_pack_ms);
            v_dec_hdr.push_back(r.hkn_dec_header_ms);
            v_dec_plane.push_back(r.hkn_dec_plane_total_ms);
            v_dec_ycocg.push_back(r.hkn_dec_ycocg_to_rgb_ms);
            v_dec_plane_dispatch.push_back(r.hkn_dec_plane_dispatch_ms);
            v_dec_plane_wait.push_back(r.hkn_dec_plane_wait_ms);
            v_dec_ycocg_dispatch.push_back(r.hkn_dec_ycocg_dispatch_ms);
            v_dec_ycocg_kernel.push_back(r.hkn_dec_ycocg_kernel_ms);
            v_dec_ycocg_wait.push_back(r.hkn_dec_ycocg_wait_ms);
            v_dec_nat.push_back(r.hkn_dec_plane_try_natural_ms);
            v_dec_screen.push_back(r.hkn_dec_plane_screen_wrapper_ms);
            v_dec_bt.push_back(r.hkn_dec_plane_block_types_ms);
            v_dec_fid.push_back(r.hkn_dec_plane_filter_ids_ms);
            v_dec_lo.push_back(r.hkn_dec_plane_filter_lo_ms);
            v_dec_hi.push_back(r.hkn_dec_plane_filter_hi_ms);
            v_dec_recon.push_back(r.hkn_dec_plane_reconstruct_ms);
            v_enc_py.push_back(r.hkn_enc_plane_y_ms);
            v_enc_pco.push_back(r.hkn_enc_plane_co_ms);
            v_enc_pcg.push_back(r.hkn_enc_plane_cg_ms);
            v_dec_py.push_back(r.hkn_dec_plane_y_ms);
            v_dec_pco.push_back(r.hkn_dec_plane_co_ms);
            v_dec_pcg.push_back(r.hkn_dec_plane_cg_ms);
            v_dec_lo_rans.push_back(r.hkn_dec_filter_lo_decode_rans_ms);
            v_dec_lo_shared_rans.push_back(r.hkn_dec_filter_lo_decode_shared_rans_ms);
            v_dec_lo_lz.push_back(r.hkn_dec_filter_lo_tilelz_ms);
            v_enc_lo_sel0.push_back(r.hkn_enc_filter_lo_mode0);
            v_enc_lo_sel1.push_back(r.hkn_enc_filter_lo_mode1);
            v_enc_lo_sel2.push_back(r.hkn_enc_filter_lo_mode2);
            v_enc_lo_sel3.push_back(r.hkn_enc_filter_lo_mode3);
            v_enc_lo_sel4.push_back(r.hkn_enc_filter_lo_mode4);
            v_enc_lo_sel5.push_back(r.hkn_enc_filter_lo_mode5);
            v_enc_lo_probe_enabled.push_back(r.hkn_enc_lo_lz_probe_enabled);
            v_enc_lo_probe_checked.push_back(r.hkn_enc_lo_lz_probe_checked);
            v_enc_lo_probe_pass.push_back(r.hkn_enc_lo_lz_probe_pass);
            v_enc_lo_probe_skip.push_back(r.hkn_enc_lo_lz_probe_skip);
            v_enc_lo_probe_sample.push_back(r.hkn_enc_lo_lz_probe_sample_bytes);
            v_enc_lo_probe_sample_lz.push_back(r.hkn_enc_lo_lz_probe_sample_lz_bytes);
            v_enc_lo_probe_sample_wrapped.push_back(r.hkn_enc_lo_lz_probe_sample_wrapped_bytes);
            v_enc_p3.push_back(r.hkn_enc_plane_parallel_3way);
            v_enc_p2.push_back(r.hkn_enc_plane_parallel_2way);
            v_enc_ps.push_back(r.hkn_enc_plane_parallel_seq);
            v_enc_ptok.push_back(r.hkn_enc_plane_parallel_tokens_sum);
            v_enc_route_par.push_back(r.hkn_enc_plane_route_parallel);
            v_enc_route_seq.push_back(r.hkn_enc_plane_route_seq);
            v_enc_route_tok.push_back(r.hkn_enc_plane_route_parallel_tokens_sum);
            v_nat_mode0_sel.push_back(r.hkn_enc_route_nat_mode0_selected);
            v_nat_mode1_sel.push_back(r.hkn_enc_route_nat_mode1_selected);
            v_nat_mode2_sel.push_back(r.hkn_enc_route_nat_mode2_selected);
            v_nat_mode3_sel.push_back(r.hkn_enc_route_nat_mode3_selected);
            v_nat_pred_raw.push_back(r.hkn_enc_route_nat_pred_raw);
            v_nat_pred_rans.push_back(r.hkn_enc_route_nat_pred_rans);
            v_nat_bias_adopt.push_back(r.hkn_enc_route_nat_mode2_bias_adopt);
            v_nat_bias_reject.push_back(r.hkn_enc_route_nat_mode2_bias_reject);
            v_nat_prep_par.push_back(r.hkn_enc_route_nat_prep_parallel);
            v_nat_prep_seq.push_back(r.hkn_enc_route_nat_prep_seq);
            v_nat_prep_tok.push_back(r.hkn_enc_route_nat_prep_tokens_sum);
            v_nat_mode12_par.push_back(r.hkn_enc_route_nat_mode12_parallel);
            v_nat_mode12_seq.push_back(r.hkn_enc_route_nat_mode12_seq);
            v_nat_mode12_tok.push_back(r.hkn_enc_route_nat_mode12_tokens_sum);
            v_nat_m2_lz_calls.push_back(r.hkn_enc_route_nat_mode2_lz_calls);
            v_nat_m2_lz_src.push_back(r.hkn_enc_route_nat_mode2_lz_src_bytes);
            v_nat_m2_lz_out.push_back(r.hkn_enc_route_nat_mode2_lz_out_bytes);
            v_nat_m2_lz_match_count.push_back(r.hkn_enc_route_nat_mode2_lz_match_count);
            v_nat_m2_lz_match_bytes.push_back(r.hkn_enc_route_nat_mode2_lz_match_bytes);
            v_nat_m2_lz_literal_bytes.push_back(r.hkn_enc_route_nat_mode2_lz_literal_bytes);
            v_nat_m2_lz_chain_steps.push_back(r.hkn_enc_route_nat_mode2_lz_chain_steps);
            v_nat_m2_lz_depth_hits.push_back(r.hkn_enc_route_nat_mode2_lz_depth_limit_hits);
            v_nat_m2_lz_maxlen_hits.push_back(r.hkn_enc_route_nat_mode2_lz_early_maxlen_hits);
            v_nat_m2_lz_nice_hits.push_back(r.hkn_enc_route_nat_mode2_lz_nice_cutoff_hits);
            v_nat_m2_lz_len3_reject.push_back(r.hkn_enc_route_nat_mode2_lz_len3_reject_dist);
            v_dec_p3.push_back(r.hkn_dec_plane_parallel_3way);
            v_dec_ps.push_back(r.hkn_dec_plane_parallel_seq);
            v_dec_ptok.push_back(r.hkn_dec_plane_parallel_tokens_sum);
            v_dec_rgb_p.push_back(r.hkn_dec_ycocg_parallel);
            v_dec_rgb_s.push_back(r.hkn_dec_ycocg_sequential);
            v_dec_rgb_thr.push_back(r.hkn_dec_ycocg_parallel_threads_sum);
            v_dec_rgb_rows.push_back(r.hkn_dec_ycocg_rows_sum);
            v_dec_rgb_pixels.push_back(r.hkn_dec_ycocg_pixels_sum);
            v_lo_raw.push_back(r.hkn_dec_filter_lo_mode_raw);
            v_lo_m1.push_back(r.hkn_dec_filter_lo_mode1);
            v_lo_m2.push_back(r.hkn_dec_filter_lo_mode2);
            v_lo_m3.push_back(r.hkn_dec_filter_lo_mode3);
            v_lo_m4.push_back(r.hkn_dec_filter_lo_mode4);
            v_lo_m5.push_back(r.hkn_dec_filter_lo_mode5);
            v_lo_inv.push_back(r.hkn_dec_filter_lo_mode_invalid);
            v_lo_fb.push_back(r.hkn_dec_filter_lo_fallback_zero_fill);
            v_lo_m4_par.push_back(r.hkn_dec_filter_lo_mode4_parallel_tiles);
            v_lo_m4_seq.push_back(r.hkn_dec_filter_lo_mode4_sequential_tiles);
            v_rc_copy_fast.push_back(r.hkn_dec_recon_copy_fast_rows);
            v_rc_copy_slow.push_back(r.hkn_dec_recon_copy_slow_rows);
            v_rc_t4_fast.push_back(r.hkn_dec_recon_tile4_fast_quads);
            v_rc_t4_slow.push_back(r.hkn_dec_recon_tile4_slow_quads);
            v_rc_res_miss.push_back(r.hkn_dec_recon_residual_missing);
        }
        const double med_enc_rgb = median_value(v_enc_rgb);
        const double med_enc_cls = median_value(v_enc_cls);
        const double med_enc_plane = median_value(v_enc_plane);
        const double med_enc_blk = median_value(v_enc_blk);
        const double med_enc_rows = median_value(v_enc_rows);
        const double med_enc_lo = median_value(v_enc_lo);
        const double med_enc_lo_m2 = median_value(v_enc_lo_m2);
        const double med_enc_lo_m3 = median_value(v_enc_lo_m3);
        const double med_enc_lo_m4 = median_value(v_enc_lo_m4);
        const double med_enc_lo_m5 = median_value(v_enc_lo_m5);
        const double med_enc_hi = median_value(v_enc_hi);
        const double med_enc_wrap = median_value(v_enc_wrap);
        const double med_enc_route = median_value(v_enc_route);
        const double med_enc_route_pref = median_value(v_enc_route_pref);
        const double med_enc_route_screen = median_value(v_enc_route_screen);
        const double med_enc_route_nat = median_value(v_enc_route_nat);
        const double med_enc_route_nat_m0 = median_value(v_enc_route_nat_m0);
        const double med_enc_route_nat_m1prep = median_value(v_enc_route_nat_m1prep);
        const double med_enc_route_nat_predpack = median_value(v_enc_route_nat_predpack);
        const double med_enc_route_nat_m1 = median_value(v_enc_route_nat_m1);
        const double med_enc_route_nat_m2 = median_value(v_enc_route_nat_m2);
        const double med_enc_route_nat_m3 = median_value(v_enc_route_nat_m3);
        const double med_enc_pack = median_value(v_enc_pack);
        const double med_dec_hdr = median_value(v_dec_hdr);
        const double med_dec_plane = median_value(v_dec_plane);
        const double med_dec_ycocg = median_value(v_dec_ycocg);
        const double med_dec_plane_dispatch = median_value(v_dec_plane_dispatch);
        const double med_dec_plane_wait = median_value(v_dec_plane_wait);
        const double med_dec_ycocg_dispatch = median_value(v_dec_ycocg_dispatch);
        const double med_dec_ycocg_kernel = median_value(v_dec_ycocg_kernel);
        const double med_dec_ycocg_wait = median_value(v_dec_ycocg_wait);
        const double med_dec_nat = median_value(v_dec_nat);
        const double med_dec_screen = median_value(v_dec_screen);
        const double med_dec_bt = median_value(v_dec_bt);
        const double med_dec_fid = median_value(v_dec_fid);
        const double med_dec_lo = median_value(v_dec_lo);
        const double med_dec_hi = median_value(v_dec_hi);
        const double med_dec_recon = median_value(v_dec_recon);
        const double med_enc_py = median_value(v_enc_py);
        const double med_enc_pco = median_value(v_enc_pco);
        const double med_enc_pcg = median_value(v_enc_pcg);
        const double med_dec_py = median_value(v_dec_py);
        const double med_dec_pco = median_value(v_dec_pco);
        const double med_dec_pcg = median_value(v_dec_pcg);
        const double med_dec_lo_rans = median_value(v_dec_lo_rans);
        const double med_dec_lo_shared_rans = median_value(v_dec_lo_shared_rans);
        const double med_dec_lo_lz = median_value(v_dec_lo_lz);
        const uint64_t med_enc_lo_sel0 = median_value(v_enc_lo_sel0);
        const uint64_t med_enc_lo_sel1 = median_value(v_enc_lo_sel1);
        const uint64_t med_enc_lo_sel2 = median_value(v_enc_lo_sel2);
        const uint64_t med_enc_lo_sel3 = median_value(v_enc_lo_sel3);
        const uint64_t med_enc_lo_sel4 = median_value(v_enc_lo_sel4);
        const uint64_t med_enc_lo_sel5 = median_value(v_enc_lo_sel5);
        const uint64_t med_enc_lo_probe_enabled = median_value(v_enc_lo_probe_enabled);
        const uint64_t med_enc_lo_probe_checked = median_value(v_enc_lo_probe_checked);
        const uint64_t med_enc_lo_probe_pass = median_value(v_enc_lo_probe_pass);
        const uint64_t med_enc_lo_probe_skip = median_value(v_enc_lo_probe_skip);
        const uint64_t med_enc_lo_probe_sample = median_value(v_enc_lo_probe_sample);
        const uint64_t med_enc_lo_probe_sample_lz = median_value(v_enc_lo_probe_sample_lz);
        const uint64_t med_enc_lo_probe_sample_wrapped = median_value(v_enc_lo_probe_sample_wrapped);
        const uint64_t med_enc_p3 = median_value(v_enc_p3);
        const uint64_t med_enc_p2 = median_value(v_enc_p2);
        const uint64_t med_enc_ps = median_value(v_enc_ps);
        const uint64_t med_enc_ptok = median_value(v_enc_ptok);
        const uint64_t med_enc_route_par = median_value(v_enc_route_par);
        const uint64_t med_enc_route_seq = median_value(v_enc_route_seq);
        const uint64_t med_enc_route_tok = median_value(v_enc_route_tok);
        const uint64_t med_nat_mode0_sel = median_value(v_nat_mode0_sel);
        const uint64_t med_nat_mode1_sel = median_value(v_nat_mode1_sel);
        const uint64_t med_nat_mode2_sel = median_value(v_nat_mode2_sel);
        const uint64_t med_nat_mode3_sel = median_value(v_nat_mode3_sel);
        const uint64_t med_nat_pred_raw = median_value(v_nat_pred_raw);
        const uint64_t med_nat_pred_rans = median_value(v_nat_pred_rans);
        const uint64_t med_nat_bias_adopt = median_value(v_nat_bias_adopt);
        const uint64_t med_nat_bias_reject = median_value(v_nat_bias_reject);
        const uint64_t med_nat_prep_par = median_value(v_nat_prep_par);
        const uint64_t med_nat_prep_seq = median_value(v_nat_prep_seq);
        const uint64_t med_nat_prep_tok = median_value(v_nat_prep_tok);
        const uint64_t med_nat_mode12_par = median_value(v_nat_mode12_par);
        const uint64_t med_nat_mode12_seq = median_value(v_nat_mode12_seq);
        const uint64_t med_nat_mode12_tok = median_value(v_nat_mode12_tok);
        const uint64_t med_nat_m2_lz_calls = median_value(v_nat_m2_lz_calls);
        const uint64_t med_nat_m2_lz_src = median_value(v_nat_m2_lz_src);
        const uint64_t med_nat_m2_lz_out = median_value(v_nat_m2_lz_out);
        const uint64_t med_nat_m2_lz_match_count = median_value(v_nat_m2_lz_match_count);
        const uint64_t med_nat_m2_lz_match_bytes = median_value(v_nat_m2_lz_match_bytes);
        const uint64_t med_nat_m2_lz_literal_bytes = median_value(v_nat_m2_lz_literal_bytes);
        const uint64_t med_nat_m2_lz_chain_steps = median_value(v_nat_m2_lz_chain_steps);
        const uint64_t med_nat_m2_lz_depth_hits = median_value(v_nat_m2_lz_depth_hits);
        const uint64_t med_nat_m2_lz_maxlen_hits = median_value(v_nat_m2_lz_maxlen_hits);
        const uint64_t med_nat_m2_lz_nice_hits = median_value(v_nat_m2_lz_nice_hits);
        const uint64_t med_nat_m2_lz_len3_reject = median_value(v_nat_m2_lz_len3_reject);
        const uint64_t med_dec_p3 = median_value(v_dec_p3);
        const uint64_t med_dec_ps = median_value(v_dec_ps);
        const uint64_t med_dec_ptok = median_value(v_dec_ptok);
        const uint64_t med_dec_rgb_p = median_value(v_dec_rgb_p);
        const uint64_t med_dec_rgb_s = median_value(v_dec_rgb_s);
        const uint64_t med_dec_rgb_thr = median_value(v_dec_rgb_thr);
        const uint64_t med_dec_rgb_rows = median_value(v_dec_rgb_rows);
        const uint64_t med_dec_rgb_pixels = median_value(v_dec_rgb_pixels);
        const uint64_t med_lo_raw = median_value(v_lo_raw);
        const uint64_t med_lo_m1 = median_value(v_lo_m1);
        const uint64_t med_lo_m2 = median_value(v_lo_m2);
        const uint64_t med_lo_m3 = median_value(v_lo_m3);
        const uint64_t med_lo_m4 = median_value(v_lo_m4);
        const uint64_t med_lo_m5 = median_value(v_lo_m5);
        const uint64_t med_lo_inv = median_value(v_lo_inv);
        const uint64_t med_lo_fb = median_value(v_lo_fb);
        const uint64_t med_lo_m4_par = median_value(v_lo_m4_par);
        const uint64_t med_lo_m4_seq = median_value(v_lo_m4_seq);
        const uint64_t med_rc_copy_fast = median_value(v_rc_copy_fast);
        const uint64_t med_rc_copy_slow = median_value(v_rc_copy_slow);
        const uint64_t med_rc_t4_fast = median_value(v_rc_t4_fast);
        const uint64_t med_rc_t4_slow = median_value(v_rc_t4_slow);
        const uint64_t med_rc_res_miss = median_value(v_rc_res_miss);
        const double med_enc_cpu_sum = med_enc_rgb + med_enc_cls + med_enc_plane + med_enc_pack;
        const double med_dec_cpu_sum = med_dec_hdr + med_dec_plane + med_dec_ycocg;

        std::cout << "\n=== HKN Stage Breakdown (median over fixed 6) ===\n";
        std::cout << "Encode wall(ms):    " << std::fixed << std::setprecision(3) << med_hkn_enc << "\n";
        std::cout << "Encode cpu_sum(ms): " << med_enc_cpu_sum;
        if (med_hkn_enc > 0.0) {
            std::cout << " (cpu/wall=" << std::setprecision(3) << (med_enc_cpu_sum / med_hkn_enc) << ")";
        }
        std::cout << "\n";
        std::cout << "  rgb_to_ycocg:      " << med_enc_rgb << " [cpu]\n";
        std::cout << "  profile_classify:  " << med_enc_cls << " [cpu]\n";
        std::cout << "  planes_total:      " << med_enc_plane << " [cpu]\n";
        std::cout << "  plane_block_class: " << med_enc_blk << "\n";
        std::cout << "  plane_filter_rows: " << med_enc_rows << "\n";
        std::cout << "  plane_lo_stream:   " << med_enc_lo << "\n";
        std::cout << "    lo_mode_eval 2/3/4/5: "
                  << med_enc_lo_m2 << " / "
                  << med_enc_lo_m3 << " / "
                  << med_enc_lo_m4 << " / "
                  << med_enc_lo_m5 << "\n";
        std::cout << "    lo_mode_selected 0/1/2/3/4/5: "
                  << med_enc_lo_sel0 << " / "
                  << med_enc_lo_sel1 << " / "
                  << med_enc_lo_sel2 << " / "
                  << med_enc_lo_sel3 << " / "
                  << med_enc_lo_sel4 << " / "
                  << med_enc_lo_sel5 << "\n";
        double med_probe_ratio = 0.0;
        if (med_enc_lo_probe_sample > 0) {
            med_probe_ratio = (double)med_enc_lo_probe_sample_wrapped /
                              (double)med_enc_lo_probe_sample;
        }
        std::cout << "    lo_lz_probe enabled/checked/pass/skip: "
                  << med_enc_lo_probe_enabled << " / "
                  << med_enc_lo_probe_checked << " / "
                  << med_enc_lo_probe_pass << " / "
                  << med_enc_lo_probe_skip
                  << " (sample raw/lz/wrapped="
                  << med_enc_lo_probe_sample << "/"
                  << med_enc_lo_probe_sample_lz << "/"
                  << med_enc_lo_probe_sample_wrapped
                  << ", wrapped/raw=" << std::fixed << std::setprecision(3)
                  << med_probe_ratio << ")\n";
        std::cout << "  plane_hi_stream:   " << med_enc_hi << "\n";
        std::cout << "  plane_stream_wrap: " << med_enc_wrap << "\n";
        std::cout << "  plane_route_comp:  " << med_enc_route << "\n";
        std::cout << "    route_prefilter: " << med_enc_route_pref << "\n";
        std::cout << "    route_screen:    " << med_enc_route_screen << "\n";
        std::cout << "    route_natural:   " << med_enc_route_nat << "\n";
        std::cout << "      nat_mode0:     " << med_enc_route_nat_m0 << "\n";
        std::cout << "      nat_mode1prep: " << med_enc_route_nat_m1prep << "\n";
        std::cout << "      nat_pred_pack: " << med_enc_route_nat_predpack << "\n";
        std::cout << "      nat_mode1:     " << med_enc_route_nat_m1 << "\n";
        std::cout << "      nat_mode2:     " << med_enc_route_nat_m2 << "\n";
        std::cout << "      nat_mode3:     " << med_enc_route_nat_m3 << "\n";
        std::cout << "      nat_mode2_lz calls/src/out: "
                  << med_nat_m2_lz_calls << "/" << med_nat_m2_lz_src << "/" << med_nat_m2_lz_out << "\n";
        std::cout << "      nat_mode2_lz match/literal bytes: "
                  << med_nat_m2_lz_match_bytes << "/" << med_nat_m2_lz_literal_bytes
                  << " (matches=" << med_nat_m2_lz_match_count << ")\n";
        std::cout << "      nat_mode2_lz chain/depth/maxlen/nice/len3rej: "
                  << med_nat_m2_lz_chain_steps << "/"
                  << med_nat_m2_lz_depth_hits << "/"
                  << med_nat_m2_lz_maxlen_hits << "/"
                  << med_nat_m2_lz_nice_hits << "/"
                  << med_nat_m2_lz_len3_reject << "\n";
        std::cout << "  container_pack:    " << med_enc_pack << " [cpu]\n";
        std::cout << "  plane_y/co/cg:     " << med_enc_py << " / " << med_enc_pco
                  << " / " << med_enc_pcg << " [cpu]\n";
        std::cout << "Decode wall(ms):    " << med_hkn_dec << "\n";
        std::cout << "Decode cpu_sum(ms): " << med_dec_cpu_sum;
        if (med_hkn_dec > 0.0) {
            std::cout << " (cpu/wall=" << std::setprecision(3) << (med_dec_cpu_sum / med_hkn_dec) << ")";
        }
        std::cout << "\n";
        std::cout << "  header_dir:        " << med_dec_hdr << " [cpu]\n";
        std::cout << "  planes_total:      " << med_dec_plane << " [cpu]\n";
        std::cout << "  plane dispatch/wait: "
                  << med_dec_plane_dispatch << " / " << med_dec_plane_wait << " [cpu]\n";
        std::cout << "  ycocg_to_rgb:      " << med_dec_ycocg << " [cpu]\n";
        std::cout << "    ycocg dispatch/kernel/wait: "
                  << med_dec_ycocg_dispatch << " / "
                  << med_dec_ycocg_kernel << " / "
                  << med_dec_ycocg_wait << " [cpu]\n";
        std::cout << "  plane_try_natural: " << med_dec_nat << " [cpu]\n";
        std::cout << "  plane_screen_wrap: " << med_dec_screen << " [cpu]\n";
        std::cout << "  plane_block_types: " << med_dec_bt << " [cpu]\n";
        std::cout << "  plane_filter_ids:  " << med_dec_fid << " [cpu]\n";
        std::cout << "  plane_filter_lo:   " << med_dec_lo << " [cpu]\n";
        std::cout << "  plane_filter_hi:   " << med_dec_hi << " [cpu]\n";
        std::cout << "  plane_reconstruct: " << med_dec_recon << " [cpu]\n";
        std::cout << "  plane_y/co/cg:     " << med_dec_py << " / " << med_dec_pco
                  << " / " << med_dec_pcg << " [cpu]\n";
        std::cout << "\n=== Parallel Counters (median per image) ===\n";
        std::cout << "encode plane scheduler  3way/2way/seq/tokens: "
                  << med_enc_p3 << "/" << med_enc_p2 << "/" << med_enc_ps
                  << "/" << med_enc_ptok << "\n";
        std::cout << "route compete scheduler parallel/seq/tokens: "
                  << med_enc_route_par << "/" << med_enc_route_seq
                  << "/" << med_enc_route_tok << "\n";
        std::cout << "route natural prep    parallel/seq/tokens: "
                  << med_nat_prep_par << "/" << med_nat_prep_seq
                  << "/" << med_nat_prep_tok << "\n";
        std::cout << "route natural m1/m2   parallel/seq/tokens: "
                  << med_nat_mode12_par << "/" << med_nat_mode12_seq
                  << "/" << med_nat_mode12_tok << "\n";
        std::cout << "route natural selected mode0/mode1/mode2/mode3: "
                  << med_nat_mode0_sel << "/" << med_nat_mode1_sel
                  << "/" << med_nat_mode2_sel << "/" << med_nat_mode3_sel << "\n";
        std::cout << "route natural pred raw/rans: "
                  << med_nat_pred_raw << "/" << med_nat_pred_rans << "\n";
        std::cout << "route natural mode2 bias adopt/reject: "
                  << med_nat_bias_adopt << "/" << med_nat_bias_reject << "\n";
        std::cout << "decode plane scheduler  3way/seq/tokens: "
                  << med_dec_p3 << "/" << med_dec_ps << "/" << med_dec_ptok << "\n";
        std::cout << "decode ycocg->rgb       parallel/seq/threads: "
                  << med_dec_rgb_p << "/" << med_dec_rgb_s << "/" << med_dec_rgb_thr << "\n";
        std::cout << "decode ycocg->rgb       rows/pixels: "
                  << med_dec_rgb_rows << "/" << med_dec_rgb_pixels << "\n";
        std::cout << "\n=== Decode Deep Counters (median per image) ===\n";
        std::cout << "filter_lo modes raw/1/2/3/4/5/invalid: "
                  << med_lo_raw << "/" << med_lo_m1 << "/" << med_lo_m2 << "/"
                  << med_lo_m3 << "/" << med_lo_m4 << "/" << med_lo_m5 << "/"
                  << med_lo_inv << "\n";
        std::cout << "filter_lo fallback_zero_fill: " << med_lo_fb << "\n";
        std::cout << "filter_lo mode4 parallel/sequential tiles: "
                  << med_lo_m4_par << "/" << med_lo_m4_seq << "\n";
        std::cout << "filter_lo inner(ms) rans/shared_rans/tilelz: "
                  << std::fixed << std::setprecision(3) << med_dec_lo_rans << "/"
                  << med_dec_lo_shared_rans << "/" << med_dec_lo_lz << "\n";
        std::cout << "reconstruct copy rows fast/slow: "
                  << med_rc_copy_fast << "/" << med_rc_copy_slow << "\n";
        std::cout << "reconstruct tile4 quads fast/slow: "
                  << med_rc_t4_fast << "/" << med_rc_t4_slow << "\n";
        std::cout << "reconstruct residual_missing: " << med_rc_res_miss << "\n";
        std::cout << "CSV saved: " << args.out_csv << "\n";

        if (!args.baseline_csv.empty()) {
            auto baseline = load_baseline_csv(args.baseline_csv);
            std::cout << "\n=== A/B Diff vs Baseline ===\n";
            std::cout << "Image       dHKN_bytes    dDec(ms)   dSelected   dGain_bytes   dLoss_bytes   d(PNG/HKN)\n";

            std::vector<double> ab_ratios;
            for (const auto& r : rows) {
                auto it = baseline.find(r.image_id);
                if (it == baseline.end()) {
                    std::cout << std::left << std::setw(10) << r.image_name << "(missing in baseline)\n";
                    continue;
                }
                const auto& b = it->second;
                long long d_hkn = (long long)r.hkn_bytes - (long long)b.hkn_bytes;
                double d_dec = r.dec_ms - b.dec_ms;
                long long d_sel = (long long)r.natural_row_selected - (long long)b.natural_row_selected;
                long long d_gain = (long long)r.gain_bytes - (long long)b.gain_bytes;
                long long d_loss = (long long)r.loss_bytes - (long long)b.loss_bytes;
                double d_ratio = r.png_over_hkn - b.png_over_hkn;

                ab_ratios.push_back(d_ratio);

                std::cout << std::left << std::setw(10) << r.image_name
                          << std::right << std::showpos
                          << std::setw(12) << d_hkn
                          << std::setw(12) << std::fixed << std::setprecision(3) << d_dec
                          << std::setw(11) << d_sel
                          << std::setw(13) << d_gain
                          << std::setw(13) << d_loss
                          << std::setw(12) << std::fixed << std::setprecision(4) << d_ratio
                          << std::noshowpos << "\n";
            }

            if (!ab_ratios.empty()) {
                double median_d_ratio = median_value(ab_ratios);
                std::cout << "\nmedian delta(PNG/HKN): "
                          << std::showpos << std::fixed << std::setprecision(4)
                          << median_d_ratio << std::noshowpos << "\n";
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
