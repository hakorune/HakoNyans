#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include <iostream>
#include <cmath>
#include <vector>

using namespace hakonyans;

/**
 * PSNR calculation
 */
double calc_psnr(const uint8_t* a, const uint8_t* b, int size) {
    double mse = 0.0;
    for (int i = 0; i < size; i++) {
        double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        mse += diff * diff;
    }
    mse /= size;
    if (mse < 1e-10) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

int main() {
    std::cout << "=== Phase 5.2 Color Codec Tests ===" << std::endl;
    
    // Test 1: 16x16 Color Gradient
    const int W = 16;
    const int H = 16;
    std::vector<uint8_t> rgb(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            rgb[i + 0] = x * 16;      // R
            rgb[i + 1] = y * 16;      // G
            rgb[i + 2] = 128;         // B
        }
    }
    
    try {
        std::cout << "Encoding 16x16 color gradient..." << std::endl;
        auto hkn = GrayscaleEncoder::encode_color(rgb.data(), W, H, 75);
        std::cout << "HKN size: " << hkn.size() << " bytes" << std::endl;
        
        std::cout << "Decoding..." << std::endl;
        int out_w, out_h;
        auto decoded = GrayscaleDecoder::decode_color(hkn, out_w, out_h);
        
        if (out_w != W || out_h != H) {
            std::cout << "FAIL: Dimensions mismatch (" << out_w << "x" << out_h << ")" << std::endl;
            return 1;
        }
        
        double psnr = calc_psnr(rgb.data(), decoded.data(), W * H * 3);
        std::cout << "PSNR: " << psnr << " dB" << std::endl;
        
        if (psnr > 30.0) {
            std::cout << "Test 1 PASS" << std::endl;
        } else {
            std::cout << "Test 1 FAIL (PSNR too low)" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    
    // Test 2: 32x32 Color Blocks
    const int W2 = 32;
    const int H2 = 32;
    std::vector<uint8_t> rgb2(W2 * H2 * 3);
    for (int y = 0; y < H2; y++) {
        for (int x = 0; x < W2; x++) {
            int i = (y * W2 + x) * 3;
            rgb2[i + 0] = (x / 8) * 64;
            rgb2[i + 1] = (y / 8) * 64;
            rgb2[i + 2] = ((x + y) / 8) * 32;
        }
    }
    
    try {
        std::cout << "\nEncoding 32x32 color blocks..." << std::endl;
        auto hkn = GrayscaleEncoder::encode_color(rgb2.data(), W2, H2, 90);
        int out_w, out_h;
        auto decoded = GrayscaleDecoder::decode_color(hkn, out_w, out_h);
        double psnr = calc_psnr(rgb2.data(), decoded.data(), W2 * H2 * 3);
        std::cout << "PSNR: " << psnr << " dB, Size: " << hkn.size() << " bytes" << std::endl;
        
        if (psnr > 28.0) {  // 4:2:0 default causes color edge blurring on step functions
            std::cout << "Test 2 PASS" << std::endl;
        } else {
            std::cout << "Test 2 FAIL" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\nAll Color Codec Tests PASSED" << std::endl;
    return 0;
}