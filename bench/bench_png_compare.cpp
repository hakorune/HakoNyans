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

    ofs << "image_id,image_name,width,height,hkn_bytes,png_bytes,png_over_hkn,dec_ms,natural_row_selected,natural_row_candidates,natural_row_selected_rate,gain_bytes,loss_bytes\n";
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
            << r.loss_bytes << "\n";
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

    // PNG size reference
    auto png_enc = encode_png(ppm.rgb_data.data(), ppm.width, ppm.height);
    row.png_bytes = png_enc.png_data.size();

    std::vector<size_t> hkn_size_samples;
    std::vector<double> dec_samples_ms;
    std::vector<uint64_t> selected_samples;
    std::vector<uint64_t> candidate_samples;
    std::vector<uint64_t> gain_samples;
    std::vector<uint64_t> loss_samples;

    for (int i = 0; i < args.warmup + args.runs; i++) {
        auto hkn = GrayscaleEncoder::encode_color_lossless(
            ppm.rgb_data.data(),
            (uint32_t)ppm.width,
            (uint32_t)ppm.height
        );
        auto stats = GrayscaleEncoder::get_lossless_mode_debug_stats();

        int dec_w = 0;
        int dec_h = 0;
        auto t0 = std::chrono::steady_clock::now();
        auto dec = GrayscaleDecoder::decode_color_lossless(hkn, dec_w, dec_h);
        auto t1 = std::chrono::steady_clock::now();
        double dec_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (dec_w != ppm.width || dec_h != ppm.height || dec != ppm.rgb_data) {
            throw std::runtime_error("Lossless roundtrip failed for " + img.rel_path);
        }

        if (i >= args.warmup) {
            hkn_size_samples.push_back(hkn.size());
            dec_samples_ms.push_back(dec_ms);
            selected_samples.push_back(stats.natural_row_selected_count);
            candidate_samples.push_back(stats.natural_row_candidate_count);
            gain_samples.push_back(stats.natural_row_gain_bytes_sum);
            loss_samples.push_back(stats.natural_row_loss_bytes_sum);
        }
    }

    row.hkn_bytes = median_value(hkn_size_samples);
    row.dec_ms = median_value(dec_samples_ms);
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
        std::cout << "Image       size_bytes(HKN/PNG)    Dec(ms)  natural_row_selected   gain_bytes  loss_bytes  PNG/HKN\n";
        for (const auto& r : rows) {
            std::ostringstream sel;
            sel << r.natural_row_selected << "/" << r.natural_row_candidates
                << " (" << std::fixed << std::setprecision(1)
                << r.natural_row_selected_rate << "%)";
            std::cout << std::left << std::setw(10) << r.image_name
                      << std::right << std::setw(12) << r.hkn_bytes << "/"
                      << std::left << std::setw(12) << r.png_bytes
                      << std::right << std::setw(9) << std::fixed << std::setprecision(3) << r.dec_ms
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
