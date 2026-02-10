#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <cmath>
#include <algorithm>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include "ppm_loader.h"
#include "png_wrapper.h"

using namespace hakonyans;

/**
 * Test image definition
 */
struct TestImage {
    std::string path;      // Relative path to PPM file
    std::string category;  // UI, Anime, Photo, Game, Natural
    std::string name;      // Short name for display
};

/**
 * Benchmark result for a single image
 */
struct BenchmarkResult {
    std::string name;
    std::string category;
    int width;
    int height;
    size_t raw_size;      // Original RGB data size

    // PNG results
    size_t png_size;
    double png_enc_ms;
    double png_dec_ms;

    // HKN results
    size_t hkn_size;
    double hkn_enc_ms;
    double hkn_dec_ms;

    // Comparison ratios
    double size_ratio;      // hkn_size / png_size (lower is better for HKN)
    double enc_speedup;     // png_enc_ms / hkn_enc_ms (higher is better for HKN)
    double dec_speedup;     // png_dec_ms / hkn_dec_ms (higher is better for HKN)
};

/**
 * Category summary statistics
 */
struct CategorySummary {
    std::string category;
    int count;
    double avg_size_ratio;
    double avg_enc_speedup;
    double avg_dec_speedup;
};

/**
 * Run benchmark on a single image
 */
BenchmarkResult benchmark_image(const TestImage& test_img, const std::string& base_dir) {
    BenchmarkResult result;
    result.name = test_img.name;
    result.category = test_img.category;

    std::string full_path = base_dir + "/" + test_img.path;

    std::cout << "Processing " << test_img.category << "/" << test_img.name << "..." << std::flush;

    // Load PPM
    auto ppm = load_ppm(full_path);
    result.width = ppm.width;
    result.height = ppm.height;
    result.raw_size = ppm.data_size();

    const int WARMUP = 2;
    const int RUNS = 5;

    // === PNG Encoding ===
    std::vector<double> png_enc_times;
    std::vector<uint8_t> png_data;

    for (int i = 0; i < WARMUP + RUNS; i++) {
        auto enc_result = encode_png(ppm.rgb_data.data(), ppm.width, ppm.height);
        if (i >= WARMUP) {
            png_enc_times.push_back(enc_result.encode_time_ms);
            if (png_data.empty()) png_data = enc_result.png_data;
        }
    }

    result.png_size = png_data.size();
    result.png_enc_ms = std::accumulate(png_enc_times.begin(), png_enc_times.end(), 0.0) / RUNS;

    // === PNG Decoding ===
    std::vector<double> png_dec_times;
    for (int i = 0; i < WARMUP + RUNS; i++) {
        auto dec_result = decode_png(png_data.data(), png_data.size());
        if (i >= WARMUP) {
            png_dec_times.push_back(dec_result.decode_time_ms);
        }
    }
    result.png_dec_ms = std::accumulate(png_dec_times.begin(), png_dec_times.end(), 0.0) / RUNS;

    // === HKN Lossless Encoding ===
    std::vector<double> hkn_enc_times;
    std::vector<uint8_t> hkn_data;

    for (int i = 0; i < WARMUP + RUNS; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        hkn_data = GrayscaleEncoder::encode_color_lossless(ppm.rgb_data.data(), ppm.width, ppm.height);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (i >= WARMUP) {
            hkn_enc_times.push_back(ms);
        }
    }

    result.hkn_size = hkn_data.size();
    result.hkn_enc_ms = std::accumulate(hkn_enc_times.begin(), hkn_enc_times.end(), 0.0) / RUNS;

    // === HKN Lossless Decoding ===
    std::vector<double> hkn_dec_times;
    for (int i = 0; i < WARMUP + RUNS; i++) {
        int dec_w, dec_h;
        auto start = std::chrono::high_resolution_clock::now();
        auto decoded = GrayscaleDecoder::decode_color_lossless(hkn_data, dec_w, dec_h);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (i >= WARMUP) {
            hkn_dec_times.push_back(ms);
        }
    }
    result.hkn_dec_ms = std::accumulate(hkn_dec_times.begin(), hkn_dec_times.end(), 0.0) / RUNS;

    // Calculate ratios
    result.size_ratio = static_cast<double>(result.hkn_size) / result.png_size;
    result.enc_speedup = result.png_enc_ms / result.hkn_enc_ms;
    result.dec_speedup = result.png_dec_ms / result.hkn_dec_ms;

    std::cout << " done (PNG:" << (result.png_size/1024) << "KB, HKN:" << (result.hkn_size/1024) << "KB)" << std::endl;

    return result;
}

/**
 * Print results table
 */
void print_results(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                         PNG vs HKN Lossless Comparison                      \n";
    std::cout << "================================================================================\n";
    std::cout << std::left << std::setw(24) << "Image"
              << std::right << std::setw(10) << "PNG(KB)"
              << std::setw(10) << "HKN(KB)"
              << std::setw(10) << "Size%"
              << std::setw(10) << "Enc"
              << std::setw(10) << "Dec"
              << std::left << std::setw(12) << "  Category" << std::endl;
    std::cout << "--------------------------------------------------------------------------------\n";

    for (const auto& r : results) {
        std::string size_str;
        if (r.size_ratio < 1.0) {
            double reduction = (1.0 - r.size_ratio) * 100;
            size_str = "-" + std::to_string(static_cast<int>(reduction)) + "%";
        } else {
            size_str = "+" + std::to_string(static_cast<int>((r.size_ratio - 1.0) * 100)) + "%";
        }

        std::cout << std::left << std::setw(24) << r.name
                  << std::right << std::setw(10) << std::fixed << std::setprecision(1) << (r.png_size / 1024.0)
                  << std::setw(10) << (r.hkn_size / 1024.0)
                  << std::setw(9) << size_str
                  << std::setw(9) << std::setprecision(2) << r.enc_speedup << "x"
                  << std::setw(9) << std::setprecision(2) << r.dec_speedup << "x"
                  << std::left << std::setw(12) << r.category << std::endl;
    }
    std::cout << "================================================================================\n";
}

/**
 * Compute category summaries
 */
std::map<std::string, CategorySummary> compute_summaries(const std::vector<BenchmarkResult>& results) {
    std::map<std::string, CategorySummary> summaries;

    for (const auto& r : results) {
        auto& sum = summaries[r.category];
        sum.category = r.category;
        sum.count++;
        sum.avg_size_ratio += r.size_ratio;
        sum.avg_enc_speedup += r.enc_speedup;
        sum.avg_dec_speedup += r.dec_speedup;
    }

    for (auto& [cat, sum] : summaries) {
        sum.avg_size_ratio /= sum.count;
        sum.avg_enc_speedup /= sum.count;
        sum.avg_dec_speedup /= sum.count;
    }

    return summaries;
}

/**
 * Print category summaries
 */
void print_summaries(const std::map<std::string, CategorySummary>& summaries) {
    std::cout << "\n=== Category Averages ===\n";
    for (const auto& [cat, sum] : summaries) {
        std::string size_str;
        if (sum.avg_size_ratio < 1.0) {
            double reduction = (1.0 - sum.avg_size_ratio) * 100;
            size_str = "-" + std::to_string(static_cast<int>(reduction)) + "%";
        } else {
            size_str = "+" + std::to_string(static_cast<int>((sum.avg_size_ratio - 1.0) * 100)) + "%";
        }

        std::cout << std::left << std::setw(12) << cat
                  << std::right << std::setw(10) << size_str
                  << std::setw(9) << std::fixed << std::setprecision(2) << sum.avg_enc_speedup << "x enc"
                  << std::setw(9) << std::setprecision(2) << sum.avg_dec_speedup << "x dec"
                  << " (n=" << sum.count << ")" << std::endl;
    }
}

/**
 * Generate markdown table for BENCHMARKS.md
 */
void generate_markdown(const std::vector<BenchmarkResult>& results,
                      const std::map<std::string, CategorySummary>& summaries) {
    std::cout << "\n=== Markdown Table for BENCHMARKS.md ===\n\n";

    std::cout << "## PNG vs HKN Lossless Comparison\n\n";
    std::cout << "**Date**: " << __DATE__ << "\n";
    std::cout << "**Hardware**: x86_64 (AVX2 enabled)\n";
    std::cout << "**Test Conditions**: PNG level 9 vs HKN Lossless (YCoCg-R + filters)\n\n";

    std::cout << "### Overall Results\n\n";
    std::cout << "| Image | Category | PNG (KB) | HKN (KB) | Size Ratio | Enc Speedup | Dec Speedup |\n";
    std::cout << "|-------|----------|----------|----------|------------|-------------|-------------|\n";

    for (const auto& r : results) {
        std::cout << "| " << r.name
                  << " | " << r.category
                  << " | " << std::fixed << std::setprecision(1) << (r.png_size / 1024.0)
                  << " | " << (r.hkn_size / 1024.0)
                  << " | " << std::setprecision(2) << r.size_ratio << "x";

        if (r.size_ratio < 1.0) {
            std::cout << " ✅";
        } else if (r.size_ratio > 1.1) {
            std::cout << " ❌";
        }

        std::cout << " | " << std::setprecision(2) << r.enc_speedup << "x"
                  << " | " << r.dec_speedup << "x |\n";
    }

    std::cout << "\n### Category Analysis\n\n";
    std::cout << "| Category | Images | Avg Size Ratio | Avg Enc Speedup | Avg Dec Speedup |\n";
    std::cout << "|----------|--------|----------------|-----------------|-----------------|\n";

    for (const auto& [cat, sum] : summaries) {
        std::cout << "| " << cat
                  << " | " << sum.count
                  << " | " << std::fixed << std::setprecision(2) << sum.avg_size_ratio << "x";

        if (sum.avg_size_ratio < 1.0) {
            std::cout << " ✅";
        } else if (sum.avg_size_ratio > 1.1) {
            std::cout << " ❌";
        }

        std::cout << " | " << std::setprecision(2) << sum.avg_enc_speedup << "x"
                  << " | " << sum.avg_dec_speedup << "x |\n";
    }
}

int main() {
    std::cout << "=== PNG vs HKN Lossless Benchmark ===" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << std::endl;

    // Test images (13 total)
    std::vector<TestImage> test_images = {
        // UI Screenshots (3)
        {"ui/browser.ppm", "UI", "browser"},
        {"ui/vscode.ppm", "UI", "vscode"},
        {"ui/terminal.ppm", "UI", "terminal"},

        // Anime (2)
        {"anime/anime_girl_portrait.ppm", "Anime", "anime_girl"},
        {"anime/anime_sunset.ppm", "Anime", "anime_sunset"},

        // Photo (2)
        {"photo/nature_01.ppm", "Photo", "nature_01"},
        {"photo/nature_02.ppm", "Photo", "nature_02"},

        // Game (2)
        {"game/minecraft_2d.ppm", "Game", "minecraft_2d"},
        {"game/retro.ppm", "Game", "retro"},

        // Natural/Kodak (4)
        {"kodak/kodim01.ppm", "Natural", "kodim01"},
        {"kodak/kodim02.ppm", "Natural", "kodim02"},
        {"kodak/kodim03.ppm", "Natural", "kodim03"},
        {"kodak/hd_01.ppm", "Natural", "hd_01"},
    };

    std::string base_dir = "../test_images";

    std::vector<BenchmarkResult> results;

    // Run benchmarks
    for (const auto& test_img : test_images) {
        try {
            auto result = benchmark_image(test_img, base_dir);
            results.push_back(result);
        } catch (const std::exception& e) {
            std::cerr << "\nError: " << e.what() << std::endl;
        }
    }

    // Print results
    print_results(results);

    // Compute and print summaries
    auto summaries = compute_summaries(results);
    print_summaries(summaries);

    // Generate markdown
    generate_markdown(results, summaries);

    std::cout << "\n=== Interpretation ===\n";
    std::cout << "Size Ratio: <1.0 = HKN smaller (better), >1.0 = PNG smaller\n";
    std::cout << "Enc/Dec Speedup: >1.0 = HKN faster (better)\n";

    return 0;
}
