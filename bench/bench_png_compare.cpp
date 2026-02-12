#include <algorithm>
#include <chrono>
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

    // HKN encode stage timings (Median)
    double hkn_enc_rgb_to_ycocg_ms = 0.0;
    double hkn_enc_profile_ms = 0.0;
    double hkn_enc_plane_total_ms = 0.0;
    double hkn_enc_plane_block_classify_ms = 0.0;
    double hkn_enc_plane_filter_rows_ms = 0.0;
    double hkn_enc_plane_lo_stream_ms = 0.0;
    double hkn_enc_plane_hi_stream_ms = 0.0;
    double hkn_enc_plane_stream_wrap_ms = 0.0;
    double hkn_enc_plane_route_ms = 0.0;
    double hkn_enc_container_pack_ms = 0.0;

    // HKN decode stage timings (Median)
    double hkn_dec_header_ms = 0.0;
    double hkn_dec_plane_total_ms = 0.0;
    double hkn_dec_ycocg_to_rgb_ms = 0.0;
    double hkn_dec_plane_try_natural_ms = 0.0;
    double hkn_dec_plane_screen_wrapper_ms = 0.0;
    double hkn_dec_plane_block_types_ms = 0.0;
    double hkn_dec_plane_filter_ids_ms = 0.0;
    double hkn_dec_plane_filter_lo_ms = 0.0;
    double hkn_dec_plane_filter_hi_ms = 0.0;
    double hkn_dec_plane_reconstruct_ms = 0.0;

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
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--base-dir DIR] [--out CSV] [--baseline CSV] [--runs N] [--warmup N]\n";
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

    ofs << "image_id,image_name,width,height,hkn_bytes,png_bytes,png_over_hkn,dec_ms,natural_row_selected,natural_row_candidates,natural_row_selected_rate,gain_bytes,loss_bytes,hkn_enc_ms,hkn_dec_ms,png_enc_ms,png_dec_ms,hkn_enc_rgb_to_ycocg_ms,hkn_enc_profile_ms,hkn_enc_plane_total_ms,hkn_enc_plane_block_classify_ms,hkn_enc_plane_filter_rows_ms,hkn_enc_plane_lo_stream_ms,hkn_enc_plane_hi_stream_ms,hkn_enc_plane_stream_wrap_ms,hkn_enc_plane_route_ms,hkn_enc_container_pack_ms,hkn_dec_header_ms,hkn_dec_plane_total_ms,hkn_dec_ycocg_to_rgb_ms,hkn_dec_plane_try_natural_ms,hkn_dec_plane_screen_wrapper_ms,hkn_dec_plane_block_types_ms,hkn_dec_plane_filter_ids_ms,hkn_dec_plane_filter_lo_ms,hkn_dec_plane_filter_hi_ms,hkn_dec_plane_reconstruct_ms\n";
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
            << r.hkn_enc_ms << ","
            << r.hkn_dec_ms << ","
            << r.png_enc_ms << ","
            << r.png_dec_ms << ","
            << r.hkn_enc_rgb_to_ycocg_ms << ","
            << r.hkn_enc_profile_ms << ","
            << r.hkn_enc_plane_total_ms << ","
            << r.hkn_enc_plane_block_classify_ms << ","
            << r.hkn_enc_plane_filter_rows_ms << ","
            << r.hkn_enc_plane_lo_stream_ms << ","
            << r.hkn_enc_plane_hi_stream_ms << ","
            << r.hkn_enc_plane_stream_wrap_ms << ","
            << r.hkn_enc_plane_route_ms << ","
            << r.hkn_enc_container_pack_ms << ","
            << r.hkn_dec_header_ms << ","
            << r.hkn_dec_plane_total_ms << ","
            << r.hkn_dec_ycocg_to_rgb_ms << ","
            << r.hkn_dec_plane_try_natural_ms << ","
            << r.hkn_dec_plane_screen_wrapper_ms << ","
            << r.hkn_dec_plane_block_types_ms << ","
            << r.hkn_dec_plane_filter_ids_ms << ","
            << r.hkn_dec_plane_filter_lo_ms << ","
            << r.hkn_dec_plane_filter_hi_ms << ","
            << r.hkn_dec_plane_reconstruct_ms << "\n";
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
    std::vector<double> hkn_enc_plane_filter_rows_samples_ms;
    std::vector<double> hkn_enc_plane_lo_stream_samples_ms;
    std::vector<double> hkn_enc_plane_hi_stream_samples_ms;
    std::vector<double> hkn_enc_plane_stream_wrap_samples_ms;
    std::vector<double> hkn_enc_plane_route_samples_ms;
    std::vector<double> hkn_enc_container_pack_samples_ms;
    std::vector<double> hkn_dec_header_samples_ms;
    std::vector<double> hkn_dec_plane_total_samples_ms;
    std::vector<double> hkn_dec_ycocg_to_rgb_samples_ms;
    std::vector<double> hkn_dec_plane_try_natural_samples_ms;
    std::vector<double> hkn_dec_plane_screen_wrapper_samples_ms;
    std::vector<double> hkn_dec_plane_block_types_samples_ms;
    std::vector<double> hkn_dec_plane_filter_ids_samples_ms;
    std::vector<double> hkn_dec_plane_filter_lo_samples_ms;
    std::vector<double> hkn_dec_plane_filter_hi_samples_ms;
    std::vector<double> hkn_dec_plane_reconstruct_samples_ms;
    std::vector<uint64_t> selected_samples;
    std::vector<uint64_t> candidate_samples;
    std::vector<uint64_t> gain_samples;
    std::vector<uint64_t> loss_samples;

    for (int i = 0; i < args.warmup + args.runs; i++) {
        auto hkn_t0 = std::chrono::steady_clock::now();
        auto hkn = GrayscaleEncoder::encode_color_lossless(
            ppm.rgb_data.data(),
            (uint32_t)ppm.width,
            (uint32_t)ppm.height
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
            hkn_enc_plane_filter_rows_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_filter_rows_ns));
            hkn_enc_plane_lo_stream_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_lo_stream_ns));
            hkn_enc_plane_hi_stream_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_hi_stream_ns));
            hkn_enc_plane_stream_wrap_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_stream_wrap_ns));
            hkn_enc_plane_route_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_plane_route_compete_ns));
            hkn_enc_container_pack_samples_ms.push_back(ns_to_ms(enc_stats.perf_encode_container_pack_ns));
            hkn_dec_header_samples_ms.push_back(ns_to_ms(dec_stats.decode_header_dir_ns));
            hkn_dec_plane_total_samples_ms.push_back(ns_to_ms(dec_stats.decode_plane_total_ns));
            hkn_dec_ycocg_to_rgb_samples_ms.push_back(ns_to_ms(dec_stats.decode_ycocg_to_rgb_ns));
            hkn_dec_plane_try_natural_samples_ms.push_back(ns_to_ms(dec_stats.plane_try_natural_ns));
            hkn_dec_plane_screen_wrapper_samples_ms.push_back(ns_to_ms(dec_stats.plane_screen_wrapper_ns));
            hkn_dec_plane_block_types_samples_ms.push_back(ns_to_ms(dec_stats.plane_block_types_ns));
            hkn_dec_plane_filter_ids_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_ids_ns));
            hkn_dec_plane_filter_lo_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_lo_ns));
            hkn_dec_plane_filter_hi_samples_ms.push_back(ns_to_ms(dec_stats.plane_filter_hi_ns));
            hkn_dec_plane_reconstruct_samples_ms.push_back(ns_to_ms(dec_stats.plane_reconstruct_ns));
            selected_samples.push_back(enc_stats.natural_row_selected_count);
            candidate_samples.push_back(enc_stats.natural_row_candidate_count);
            gain_samples.push_back(enc_stats.natural_row_gain_bytes_sum);
            loss_samples.push_back(enc_stats.natural_row_loss_bytes_sum);
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
    row.hkn_enc_plane_filter_rows_ms = median_value(hkn_enc_plane_filter_rows_samples_ms);
    row.hkn_enc_plane_lo_stream_ms = median_value(hkn_enc_plane_lo_stream_samples_ms);
    row.hkn_enc_plane_hi_stream_ms = median_value(hkn_enc_plane_hi_stream_samples_ms);
    row.hkn_enc_plane_stream_wrap_ms = median_value(hkn_enc_plane_stream_wrap_samples_ms);
    row.hkn_enc_plane_route_ms = median_value(hkn_enc_plane_route_samples_ms);
    row.hkn_enc_container_pack_ms = median_value(hkn_enc_container_pack_samples_ms);
    row.hkn_dec_header_ms = median_value(hkn_dec_header_samples_ms);
    row.hkn_dec_plane_total_ms = median_value(hkn_dec_plane_total_samples_ms);
    row.hkn_dec_ycocg_to_rgb_ms = median_value(hkn_dec_ycocg_to_rgb_samples_ms);
    row.hkn_dec_plane_try_natural_ms = median_value(hkn_dec_plane_try_natural_samples_ms);
    row.hkn_dec_plane_screen_wrapper_ms = median_value(hkn_dec_plane_screen_wrapper_samples_ms);
    row.hkn_dec_plane_block_types_ms = median_value(hkn_dec_plane_block_types_samples_ms);
    row.hkn_dec_plane_filter_ids_ms = median_value(hkn_dec_plane_filter_ids_samples_ms);
    row.hkn_dec_plane_filter_lo_ms = median_value(hkn_dec_plane_filter_lo_samples_ms);
    row.hkn_dec_plane_filter_hi_ms = median_value(hkn_dec_plane_filter_hi_samples_ms);
    row.hkn_dec_plane_reconstruct_ms = median_value(hkn_dec_plane_reconstruct_samples_ms);
    row.dec_ms = row.hkn_dec_ms;
    row.natural_row_selected = median_value(selected_samples);
    row.natural_row_candidates = median_value(candidate_samples);
    row.gain_bytes = median_value(gain_samples);
    row.loss_bytes = median_value(loss_samples);

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

        std::vector<double> v_enc_rgb, v_enc_cls, v_enc_plane, v_enc_blk, v_enc_rows, v_enc_lo, v_enc_hi, v_enc_wrap, v_enc_route, v_enc_pack;
        std::vector<double> v_dec_hdr, v_dec_plane, v_dec_ycocg, v_dec_nat, v_dec_screen, v_dec_bt, v_dec_fid, v_dec_lo, v_dec_hi, v_dec_recon;
        v_enc_rgb.reserve(rows.size());
        v_enc_cls.reserve(rows.size());
        v_enc_plane.reserve(rows.size());
        v_enc_blk.reserve(rows.size());
        v_enc_rows.reserve(rows.size());
        v_enc_lo.reserve(rows.size());
        v_enc_hi.reserve(rows.size());
        v_enc_wrap.reserve(rows.size());
        v_enc_route.reserve(rows.size());
        v_enc_pack.reserve(rows.size());
        v_dec_hdr.reserve(rows.size());
        v_dec_plane.reserve(rows.size());
        v_dec_ycocg.reserve(rows.size());
        v_dec_nat.reserve(rows.size());
        v_dec_screen.reserve(rows.size());
        v_dec_bt.reserve(rows.size());
        v_dec_fid.reserve(rows.size());
        v_dec_lo.reserve(rows.size());
        v_dec_hi.reserve(rows.size());
        v_dec_recon.reserve(rows.size());
        for (const auto& r : rows) {
            v_enc_rgb.push_back(r.hkn_enc_rgb_to_ycocg_ms);
            v_enc_cls.push_back(r.hkn_enc_profile_ms);
            v_enc_plane.push_back(r.hkn_enc_plane_total_ms);
            v_enc_blk.push_back(r.hkn_enc_plane_block_classify_ms);
            v_enc_rows.push_back(r.hkn_enc_plane_filter_rows_ms);
            v_enc_lo.push_back(r.hkn_enc_plane_lo_stream_ms);
            v_enc_hi.push_back(r.hkn_enc_plane_hi_stream_ms);
            v_enc_wrap.push_back(r.hkn_enc_plane_stream_wrap_ms);
            v_enc_route.push_back(r.hkn_enc_plane_route_ms);
            v_enc_pack.push_back(r.hkn_enc_container_pack_ms);
            v_dec_hdr.push_back(r.hkn_dec_header_ms);
            v_dec_plane.push_back(r.hkn_dec_plane_total_ms);
            v_dec_ycocg.push_back(r.hkn_dec_ycocg_to_rgb_ms);
            v_dec_nat.push_back(r.hkn_dec_plane_try_natural_ms);
            v_dec_screen.push_back(r.hkn_dec_plane_screen_wrapper_ms);
            v_dec_bt.push_back(r.hkn_dec_plane_block_types_ms);
            v_dec_fid.push_back(r.hkn_dec_plane_filter_ids_ms);
            v_dec_lo.push_back(r.hkn_dec_plane_filter_lo_ms);
            v_dec_hi.push_back(r.hkn_dec_plane_filter_hi_ms);
            v_dec_recon.push_back(r.hkn_dec_plane_reconstruct_ms);
        }
        const double med_enc_rgb = median_value(v_enc_rgb);
        const double med_enc_cls = median_value(v_enc_cls);
        const double med_enc_plane = median_value(v_enc_plane);
        const double med_enc_blk = median_value(v_enc_blk);
        const double med_enc_rows = median_value(v_enc_rows);
        const double med_enc_lo = median_value(v_enc_lo);
        const double med_enc_hi = median_value(v_enc_hi);
        const double med_enc_wrap = median_value(v_enc_wrap);
        const double med_enc_route = median_value(v_enc_route);
        const double med_enc_pack = median_value(v_enc_pack);
        const double med_dec_hdr = median_value(v_dec_hdr);
        const double med_dec_plane = median_value(v_dec_plane);
        const double med_dec_ycocg = median_value(v_dec_ycocg);
        const double med_dec_nat = median_value(v_dec_nat);
        const double med_dec_screen = median_value(v_dec_screen);
        const double med_dec_bt = median_value(v_dec_bt);
        const double med_dec_fid = median_value(v_dec_fid);
        const double med_dec_lo = median_value(v_dec_lo);
        const double med_dec_hi = median_value(v_dec_hi);
        const double med_dec_recon = median_value(v_dec_recon);
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
        std::cout << "  plane_hi_stream:   " << med_enc_hi << "\n";
        std::cout << "  plane_stream_wrap: " << med_enc_wrap << "\n";
        std::cout << "  plane_route_comp:  " << med_enc_route << "\n";
        std::cout << "  container_pack:    " << med_enc_pack << " [cpu]\n";
        std::cout << "Decode wall(ms):    " << med_hkn_dec << "\n";
        std::cout << "Decode cpu_sum(ms): " << med_dec_cpu_sum;
        if (med_hkn_dec > 0.0) {
            std::cout << " (cpu/wall=" << std::setprecision(3) << (med_dec_cpu_sum / med_hkn_dec) << ")";
        }
        std::cout << "\n";
        std::cout << "  header_dir:        " << med_dec_hdr << " [cpu]\n";
        std::cout << "  planes_total:      " << med_dec_plane << " [cpu]\n";
        std::cout << "  ycocg_to_rgb:      " << med_dec_ycocg << " [cpu]\n";
        std::cout << "  plane_try_natural: " << med_dec_nat << " [cpu]\n";
        std::cout << "  plane_screen_wrap: " << med_dec_screen << " [cpu]\n";
        std::cout << "  plane_block_types: " << med_dec_bt << " [cpu]\n";
        std::cout << "  plane_filter_ids:  " << med_dec_fid << " [cpu]\n";
        std::cout << "  plane_filter_lo:   " << med_dec_lo << " [cpu]\n";
        std::cout << "  plane_filter_hi:   " << med_dec_hi << " [cpu]\n";
        std::cout << "  plane_reconstruct: " << med_dec_recon << " [cpu]\n";
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
