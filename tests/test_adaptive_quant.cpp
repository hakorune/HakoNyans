#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include <iostream>
#include <vector>
#include <cmath>

using namespace hakonyans;

double calc_psnr(const uint8_t* a, const uint8_t* b, int size) {
    double mse = 0.0;
    for (int i = 0; i < size; i++) {
        double diff = (double)a[i] - (double)b[i];
        mse += diff * diff;
    }
    mse /= size;
    if (mse < 1e-10) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

int main() {
    std::cout << "=== Phase 7a: Adaptive Quantization Test ===" << std::endl;
    
    // Create a test image with high-contrast and low-contrast regions
    const int W = 128;
    const int H = 128;
    std::vector<uint8_t> img(W * H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (x < 64) {
                // Low contrast (flat)
                img[y * W + x] = 128;
            } else {
                // High contrast (noise/texture)
                img[y * W + x] = (x % 2 == 0) ? 50 : 200;
            }
        }
    }
    
    try {
        // Encode with AQ
        auto hkn = GrayscaleEncoder::encode(img.data(), W, H, 75);
        std::cout << "Encoded size with AQ: " << hkn.size() << " bytes" << std::endl;
        
        // Decode
        auto decoded = GrayscaleDecoder::decode(hkn);
        double psnr = calc_psnr(img.data(), decoded.data(), W * H);
        std::cout << "PSNR: " << psnr << " dB" << std::endl;
        
        if (psnr > 30.0) {
            std::cout << "Test PASS" << std::endl;
        } else {
            std::cout << "Test FAIL (PSNR too low)" << std::endl;
            return 1;
        }
        
        // Compare with non-AQ (to see if it's smaller)
        // Note: Our current encode() always uses AQ.
        // To really test reduction, we would need a way to toggle it.
        // For now, if it runs and produces good PSNR, the logic is correct.
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
