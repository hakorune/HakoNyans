#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <chrono>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include "png_wrapper.h"

using namespace hakonyans;

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
    if (mse < 1e-10) return 999.0;
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
 * Get file size
 */
size_t get_file_size(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return 0;
    return static_cast<size_t>(f.tellg());
}

/**
 * Lossy compression result
 */
struct LossyResult {
    std::string name;
    int quality;
    size_t hkn_size;
    double hkn_psnr;
    double hkn_encode_ms;
    double hkn_decode_ms;
    size_t jpeg_size;
    double jpeg_psnr;
};

LossyResult test_quality(const std::string& name, const uint8_t* rgb_data, int width, int height, 
                         int quality, const std::string& output_dir) {
    LossyResult result;
    result.name = name;
    result.quality = quality;
    
    size_t data_size = width * height * 3;
    
    // === HKN Lossy ===
    {
        auto start = std::chrono::high_resolution_clock::now();
        auto hkn_data = GrayscaleEncoder::encode_color(rgb_data, width, height, quality);
        auto end_enc = std::chrono::high_resolution_clock::now();
        result.hkn_size = hkn_data.size();
        result.hkn_encode_ms = std::chrono::duration<double, std::milli>(end_enc - start).count();
        
        int dec_w, dec_h;
        auto decoded = GrayscaleDecoder::decode_color(hkn_data, dec_w, dec_h);
        auto end_dec = std::chrono::high_resolution_clock::now();
        result.hkn_decode_ms = std::chrono::duration<double, std::milli>(end_dec - end_enc).count();
        
        result.hkn_psnr = calculate_psnr(rgb_data, decoded.data(), data_size);
        
        // Save decoded image
        auto png_result = encode_png(decoded.data(), width, height);
        save_file(output_dir + "/" + name + "_hkn_q" + std::to_string(quality) + ".png", png_result.png_data);
        
        // Save HKN file
        save_file(output_dir + "/" + name + "_q" + std::to_string(quality) + ".hkn", hkn_data);
    }
    
    // === JPEG ===
    {
        std::string temp_png = output_dir + "/" + name + "_temp.png";
        auto png_temp = encode_png(rgb_data, width, height);
        save_file(temp_png, png_temp.png_data);
        
        std::string jpeg_path = output_dir + "/" + name + "_jpeg_q" + std::to_string(quality) + ".jpg";
        if (run_imagemagick(temp_png, jpeg_path, quality)) {
            result.jpeg_size = get_file_size(jpeg_path);
            
            // Decode JPEG to measure PSNR
            std::string jpeg_decoded = output_dir + "/" + name + "_jpeg_decoded_temp.png";
            if (run_imagemagick(jpeg_path, jpeg_decoded, 100)) {
                auto decoded_png = load_png_file(jpeg_decoded);
                result.jpeg_psnr = calculate_psnr(rgb_data, decoded_png.rgb_data.data(), data_size);
                std::filesystem::remove(jpeg_decoded);
            }
        }
        
        std::filesystem::remove(temp_png);
    }
    
    return result;
}

int main() {
    std::cout << "=== Anime Lossy Compression Benchmark ===" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::endl;
    
    // Test images
    std::vector<std::pair<std::string, std::string>> test_images = {
        {"../test_images/anime/Nitocris (Tottori Sand Dunes, Tottori) by Shima Udon.png", "nitocris"},
    };
    
    std::vector<int> qualities = {30, 50, 70, 90};
    
    std::string output_dir = "bench_results/anime_lossy";
    std::filesystem::create_directories(output_dir);
    
    for (const auto& [path, name] : test_images) {
        std::cout << "Processing " << name << " (" << path << ")..." << std::endl;
        
        try {
            auto png = load_png_file(path);
            std::cout << "  Loaded: " << png.width << "x" << png.height << std::endl;
            
            std::vector<LossyResult> results;
            
            for (int quality : qualities) {
                std::cout << "  Testing Q" << quality << "..." << std::flush;
                auto result = test_quality(name, png.rgb_data.data(), png.width, png.height, 
                                          quality, output_dir);
                results.push_back(result);
                std::cout << " done" << std::endl;
            }
            
            // Print results
            std::cout << "\n=== Results for " << name << " ===" << std::endl;
            std::cout << std::left << std::setw(10) << "Quality"
                      << std::right << std::setw(12) << "HKN Size"
                      << std::setw(12) << "HKN PSNR"
                      << std::setw(12) << "JPEG Size"
                      << std::setw(12) << "JPEG PSNR"
                      << std::setw(12) << "Size Ratio"
                      << std::setw(12) << "Enc (ms)"
                      << std::setw(12) << "Dec (ms)" << std::endl;
            std::cout << std::string(100, '-') << std::endl;
            
            for (const auto& r : results) {
                double ratio = static_cast<double>(r.hkn_size) / r.jpeg_size;
                std::cout << std::left << std::setw(10) << ("Q" + std::to_string(r.quality))
                          << std::right << std::setw(10) << (r.hkn_size / 1024) << "KB"
                          << std::setw(10) << std::fixed << std::setprecision(2) << r.hkn_psnr << "dB"
                          << std::setw(10) << (r.jpeg_size / 1024) << "KB"
                          << std::setw(10) << r.jpeg_psnr << "dB"
                          << std::setw(11) << std::setprecision(2) << ratio << "x"
                          << std::setw(11) << std::setprecision(0) << r.hkn_encode_ms
                          << std::setw(11) << r.hkn_decode_ms << std::endl;
            }
            std::cout << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    
    std::cout << "Results saved to: " << output_dir << std::endl;
    
    return 0;
}
