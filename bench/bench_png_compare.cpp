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

#include "bench_png_compare_common.h"
#include "bench_png_compare_runner.h"

using namespace hakonyans;

using namespace bench_png_compare_common;

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
            rows.push_back(bench_png_compare_runner::benchmark_one(img, args));
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

        #include "bench_png_compare_main_breakdown.inc"

        #include "bench_png_compare_main_abdiff.inc"

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
