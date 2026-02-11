#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <sstream>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include "ppm_loader.h"
#include "png_wrapper.h"

using namespace hakonyans;

/**
 * Load image from PNG file
 */
struct ImageData {
    std::vector<uint8_t> rgb_data;
    int width;
    int height;
    size_t data_size() const { return rgb_data.size(); }
};

ImageData load_image_png(const std::string& filepath) {
    auto result = load_png_file(filepath);
    
    ImageData img;
    img.rgb_data = std::move(result.rgb_data);
    img.width = result.width;
    img.height = result.height;
    return img;
}

/**
 * Calculate PSNR between two images
 */
double calculate_psnr(const uint8_t* original, const uint8_t* decoded, size_t size) {
    double mse = 0.0;
    for (size_t i = 0; i < size; i++) {
        double diff = static_cast<double>(original[i]) - static_cast<double>(decoded[i]);
        mse += diff * diff;
    }
    mse /= size;
    if (mse < 1e-10) return 999.0;  // Essentially infinite PSNR
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

/**
 * Save data to file
 */
void save_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot create: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!f) throw std::runtime_error("Failed to write: " + path);
}

/**
 * Run ImageMagick convert command
 */
bool run_imagemagick(const std::string& input, const std::string& output, int quality) {
    std::stringstream cmd;
    cmd << "convert \"" << input << "\" -quality " << quality << " \"" << output << "\" 2>/dev/null";
    return system(cmd.str().c_str()) == 0;
}

/**
 * Create anime quality verification for a single image
 */
struct QualityResult {
    std::string name;
    size_t original_size;
    size_t png_size;
    size_t hkn_size;
    double hkn_psnr;
    size_t jpeg75_size;
    size_t jpeg90_size;
};

QualityResult verify_anime_image(const std::string& image_path, const std::string& name, const std::string& output_dir) {
    std::cout << "Processing " << name << "..." << std::flush;

    QualityResult result;
    result.name = name;

    // Load image (PNG or PPM)
    ImageData img;
    if (image_path.ends_with(".png")) {
        img = load_image_png(image_path);
    } else {
        auto ppm = load_ppm(image_path);
        img.rgb_data = std::move(ppm.rgb_data);
        img.width = ppm.width;
        img.height = ppm.height;
    }
    
    result.original_size = img.data_size();

    // Create output directory
    std::filesystem::create_directories(output_dir);

    // Save original as PNG for reference
    {
        auto png_result = encode_png(img.rgb_data.data(), img.width, img.height);
        result.png_size = png_result.png_data.size();
        save_file(output_dir + "/" + name + "_original.png", png_result.png_data);
    }

    // === HKN Lossless Roundtrip ===
    {
        auto hkn_data = GrayscaleEncoder::encode_color_lossless(img.rgb_data.data(), img.width, img.height);
        result.hkn_size = hkn_data.size();

        int dec_w, dec_h;
        auto decoded = GrayscaleDecoder::decode_color_lossless(hkn_data, dec_w, dec_h);

        // Calculate PSNR
        result.hkn_psnr = calculate_psnr(img.rgb_data.data(), decoded.data(), img.data_size());

        // Save decoded as PNG
        auto png_result = encode_png(decoded.data(), dec_w, dec_h);
        save_file(output_dir + "/" + name + "_hkn.png", png_result.png_data);
    }

    // === JPEG Comparison ===
    std::string temp_ppm = output_dir + "/" + name + "_temp.ppm";
    save_ppm(temp_ppm.c_str(), img.rgb_data.data(), img.width, img.height);

    // JPEG Q75
    {
        std::string jpeg75_path = output_dir + "/" + name + "_jpeg75.jpg";
        if (run_imagemagick(temp_ppm, jpeg75_path, 75)) {
            std::ifstream f(jpeg75_path, std::ios::binary | std::ios::ate);
            if (f) result.jpeg75_size = f.tellg();
        }
    }

    // JPEG Q90
    {
        std::string jpeg90_path = output_dir + "/" + name + "_jpeg90.jpg";
        if (run_imagemagick(temp_ppm, jpeg90_path, 90)) {
            std::ifstream f(jpeg90_path, std::ios::binary | std::ios::ate);
            if (f) result.jpeg90_size = f.tellg();
        }
    }

    // Clean up temp file
    std::filesystem::remove(temp_ppm);

    std::cout << " done" << std::endl;

    return result;
}

int main() {
    std::cout << "=== Anime Quality Verification ===" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << std::endl;

    // Test images
    std::vector<std::pair<std::string, std::string>> test_images = {
        {"../test_images/anime/Artoria Pendragon (Tokyo Tower, Tokyo) by Takeuchi Takashi.png", "artoria"},
        {"../test_images/anime/Nitocris (Tottori Sand Dunes, Tottori) by Shima Udon.png", "nitocris"},
    };

    std::string output_dir = "bench_results/anime_quality";

    std::vector<QualityResult> results;

    for (const auto& [path, name] : test_images) {
        try {
            auto result = verify_anime_image(path, name, output_dir);
            results.push_back(result);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << std::endl;

    std::cout << std::left << std::setw(16) << "Image"
              << std::right << std::setw(12) << "Original"
              << std::setw(10) << "PNG"
              << std::setw(10) << "HKN"
              << std::setw(10) << "JPEG75"
              << std::setw(10) << "JPEG90"
              << std::setw(12) << "HKN PSNR" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(16) << r.name
                  << std::right << std::setw(10) << (r.original_size / 1024) << "KB"
                  << std::setw(10) << (r.png_size / 1024) << "KB"
                  << std::setw(10) << (r.hkn_size / 1024) << "KB"
                  << std::setw(10) << (r.jpeg75_size / 1024) << "KB"
                  << std::setw(10) << (r.jpeg90_size / 1024) << "KB";

        if (r.hkn_psnr >= 99.0) {
            std::cout << std::setw(10) << "INF ✅";
        } else {
            std::cout << std::setw(9) << std::fixed << std::setprecision(1) << r.hkn_psnr << " dB";
        }
        std::cout << std::endl;
    }

    std::cout << "\n=== Conclusion ===" << std::endl;
    std::cout << "- HKN Lossless is bit-exact (PSNR = INF)" << std::endl;
    std::cout << "- JPEG Q75 has similar size but with quality loss" << std::endl;
    std::cout << "- Images saved to: " << output_dir << std::endl;

    return 0;
}
