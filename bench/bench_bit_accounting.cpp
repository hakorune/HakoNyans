#include <algorithm>
#include <iostream>
#include <string>

#include "../src/codec/encode.h"
#include "bench_bit_accounting_common.h"
#include "bench_bit_accounting_lossless_report.h"
#include "ppm_loader.h"

using namespace hakonyans;
using namespace bench_bit_accounting;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <image.ppm> [--quality Q] [--lossless] [--lossy] [--json]"
                  << " [--preset fast|balanced|max]\n";
        return 1;
    }

    std::string path = argv[1];
    int quality = 75;
    bool do_lossless = true;
    bool do_lossy = true;
    bool json_output = false;
    GrayscaleEncoder::LosslessPreset lossless_preset = GrayscaleEncoder::LosslessPreset::BALANCED;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--quality" && i + 1 < argc) {
            quality = std::clamp(std::stoi(argv[++i]), 1, 100);
        } else if (arg == "--lossless") {
            do_lossy = false;
            do_lossless = true;
        } else if (arg == "--lossy") {
            do_lossless = false;
            do_lossy = true;
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--preset" && i + 1 < argc) {
            std::string preset = argv[++i];
            if (preset == "fast") {
                lossless_preset = GrayscaleEncoder::LosslessPreset::FAST;
            } else if (preset == "balanced") {
                lossless_preset = GrayscaleEncoder::LosslessPreset::BALANCED;
            } else if (preset == "max") {
                lossless_preset = GrayscaleEncoder::LosslessPreset::MAX;
            } else {
                std::cerr << "Invalid --preset value: " << preset
                          << " (expected fast|balanced|max)\n";
                return 1;
            }
        }
    }
    if (json_output && !do_lossless) {
        std::cerr << "--json currently supports --lossless output.\n";
        return 1;
    }

    auto ppm = load_ppm(path);
    if (!json_output) {
        std::cout << "Image: " << path << " (" << ppm.width << "x" << ppm.height << ")\n";
    }

    if (do_lossless) {
        auto hkn = GrayscaleEncoder::encode_color_lossless(
            ppm.rgb_data.data(), ppm.width, ppm.height, lossless_preset
        );
        auto mode_stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
        auto a = analyze_file(hkn);
        if (json_output) {
            print_lossless_json(path, ppm.width, ppm.height, a, mode_stats);
        } else {
            print_accounting("Lossless", a, true);
            print_lossless_mode_stats(mode_stats);
        }
    }

    if (do_lossy) {
        auto hkn = GrayscaleEncoder::encode_color(
            ppm.rgb_data.data(), ppm.width, ppm.height, (uint8_t)quality, true, true, false
        );
        auto a = analyze_file(hkn);
        if (!json_output) {
            print_accounting("Lossy (Q=" + std::to_string(quality) + ")", a, false);
        }
    }

    return 0;
}
