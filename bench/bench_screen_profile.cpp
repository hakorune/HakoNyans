#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <cstdio>
#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;

// Load PPM image
std::vector<uint8_t> load_ppm(const char* path, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    // Read magic
    std::string magic;
    std::getline(f, magic);
    if (magic != "P6") return {};

    // Skip comments
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        // Parse dimensions from this line
        if (sscanf(line.c_str(), "%d %d", &w, &h) == 2) break;
    }

    // Read maxval
    int maxval;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (sscanf(line.c_str(), "%d", &maxval) == 1) break;
    }

    // Read pixel data
    std::vector<uint8_t> rgb(w * h * 3);
    f.read((char*)rgb.data(), w * h * 3);
    return rgb;
}

// Calculate PSNR
double calc_psnr(const uint8_t* orig, const uint8_t* decoded, int size) {
    double mse = 0.0;
    for (int i = 0; i < size; i++) {
        double diff = (double)orig[i] - (double)decoded[i];
        mse += diff * diff;
    }
    mse /= size;
    if (mse < 1e-10) return 99.9;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Test image info
struct TestImage {
    const char* path;
    const char* category;
    const char* name;
};

int main() {
    std::cout << "=== HakoNyans Screen Profile Benchmark ===" << std::endl;
    std::cout << "===========================================" << std::endl;
    std::cout << std::endl;

    // Test images - add more as needed
    std::vector<TestImage> test_images = {
        // Photos (should see minimal benefit or slight regression)
        { "../test_images/kodak/kodim01.ppm", "Photo", "kodim01" },
        { "../test_images/kodak/kodim02.ppm", "Photo", "kodim02" },
        { "../test_images/kodak/kodim03.ppm", "Photo", "kodim03" },
        { "../test_images/kodak/hd_01.ppm", "Photo", "hd_01" },
        // UI Screenshots (should see significant benefit)
        { "../test_images/ui/browser.ppm", "UI", "browser" },
        { "../test_images/ui/vscode.ppm", "UI", "vscode" },
        { "../test_images/ui/terminal.ppm", "UI", "terminal" },
        // Game Screens (should see moderate benefit)
        { "../test_images/game/minecraft_2d.ppm", "Game", "minecraft_2d" },
        { "../test_images/game/retro.ppm", "Game", "retro" },
    };

    const int quality = 75;
    const bool use_420 = true;
    const bool use_cfl = true;

    std::cout << "Quality: " << quality << std::endl;
    std::cout << "Subsampling: " << (use_420 ? "4:2:0" : "4:4:4") << std::endl;
    std::cout << "CfL: " << (use_cfl ? "enabled" : "disabled") << std::endl;
    std::cout << std::endl;

    // Header
    std::cout << std::left << std::setw(20) << "Image"
              << std::right << std::setw(10) << "Baseline"
              << std::setw(10) << "ScreenProf"
              << std::setw(10) << "SizeΔ%"
              << std::setw(10) << "PSNRΔ(dB)"
              << std::setw(10) << "EncSpd↑"
              << std::setw(10) << "DecSpd↑"
              << std::left << std::setw(15) << "  Category" << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    // Category summaries
    struct Summary {
        int count = 0;
        double total_size_ratio = 0.0;
        double total_psnr_delta = 0.0;
        double total_enc_speedup = 0.0;
        double total_dec_speedup = 0.0;
    };
    std::map<std::string, Summary> summaries;

    for (const auto& test_img : test_images) {
        int w, h;
        std::vector<uint8_t> orig = load_ppm(test_img.path, w, h);
        if (orig.empty()) {
            std::cerr << "Warning: Could not load " << test_img.path << std::endl;
            continue;
        }

        int pixel_count = w * h * 3;

        // Baseline: Screen Profile disabled
        auto start_enc = std::chrono::high_resolution_clock::now();
        auto hkn_baseline = GrayscaleEncoder::encode_color(orig.data(), w, h, quality, use_420, use_cfl, false);
        auto end_enc = std::chrono::high_resolution_clock::now();
        double enc_time_baseline = std::chrono::duration<double, std::milli>(end_enc - start_enc).count();

        // Decode baseline
        int dec_w, dec_h;
        auto start_dec = std::chrono::high_resolution_clock::now();
        auto decoded_baseline = GrayscaleDecoder::decode_color(hkn_baseline, dec_w, dec_h);
        auto end_dec = std::chrono::high_resolution_clock::now();
        double dec_time_baseline = std::chrono::duration<double, std::milli>(end_dec - start_dec).count();

        // Screen Profile enabled
        start_enc = std::chrono::high_resolution_clock::now();
        auto hkn_screen = GrayscaleEncoder::encode_color(orig.data(), w, h, quality, use_420, use_cfl, true);
        end_enc = std::chrono::high_resolution_clock::now();
        double enc_time_screen = std::chrono::duration<double, std::milli>(end_enc - start_enc).count();

        // Decode screen profile
        start_dec = std::chrono::high_resolution_clock::now();
        auto decoded_screen = GrayscaleDecoder::decode_color(hkn_screen, dec_w, dec_h);
        end_dec = std::chrono::high_resolution_clock::now();
        double dec_time_screen = std::chrono::duration<double, std::milli>(end_dec - start_dec).count();

        // Calculate metrics
        double psnr_baseline = calc_psnr(orig.data(), decoded_baseline.data(), pixel_count);
        double psnr_screen = calc_psnr(orig.data(), decoded_screen.data(), pixel_count);
        double size_ratio = 100.0 * ((double)hkn_screen.size() / hkn_baseline.size() - 1.0);
        double psnr_delta = psnr_screen - psnr_baseline;
        double enc_speedup = enc_time_baseline / enc_time_screen;
        double dec_speedup = dec_time_baseline / dec_time_screen;

        // Output
        std::cout << std::left << std::setw(20) << test_img.name
                  << std::right << std::setw(10) << hkn_baseline.size()
                  << std::setw(10) << hkn_screen.size()
                  << std::setw(9) << std::fixed << std::setprecision(1) << size_ratio << "%"
                  << std::setw(9) << std::setprecision(2) << (psnr_delta >= 0 ? "+" : "") << psnr_delta
                  << std::setw(9) << std::setprecision(2) << enc_speedup << "x"
                  << std::setw(9) << std::setprecision(2) << dec_speedup << "x"
                  << std::left << std::setw(15) << test_img.category << std::endl;

        // Update summary
        auto& sum = summaries[test_img.category];
        sum.count++;
        sum.total_size_ratio += size_ratio;
        sum.total_psnr_delta += psnr_delta;
        sum.total_enc_speedup += enc_speedup;
        sum.total_dec_speedup += dec_speedup;
    }

    // Print category summaries
    std::cout << std::endl;
    std::cout << "=== Category Averages ===" << std::endl;
    for (const auto& [category, sum] : summaries) {
        if (sum.count == 0) continue;
        std::cout << std::left << std::setw(15) << category
                  << std::right << std::setw(9) << std::fixed << std::setprecision(1) << (sum.total_size_ratio / sum.count) << "%"
                  << std::setw(9) << std::setprecision(2) << (sum.total_psnr_delta / sum.count) << " dB"
                  << std::setw(9) << std::setprecision(2) << (sum.total_enc_speedup / sum.count) << "x enc"
                  << std::setw(9) << std::setprecision(2) << (sum.total_dec_speedup / sum.count) << "x dec"
                  << " (n=" << sum.count << ")" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Interpretation ===" << std::endl;
    std::cout << "SizeΔ%: Negative = better compression" << std::endl;
    std::cout << "PSNRΔ:  Positive = better quality" << std::endl;
    std::cout << "Enc/DecSpd↑: >1.0 = faster" << std::endl;

    return 0;
}
