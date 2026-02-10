#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include <iostream>
#include <cmath>
#include <vector>

using namespace hakonyans;

/**
 * Calculate PSNR between two images
 */
double calculate_psnr(const uint8_t* img1, const uint8_t* img2, size_t size) {
    double mse = 0.0;
    for (size_t i = 0; i < size; i++) {
        double diff = static_cast<double>(img1[i]) - static_cast<double>(img2[i]);
        mse += diff * diff;
    }
    mse /= size;
    
    if (mse < 1e-10) return 100.0;  // Perfect match
    
    double psnr = 10.0 * std::log10(255.0 * 255.0 / mse);
    return psnr;
}

/**
 * Test 8×8 block (single DCT block)
 */
void test_8x8_block() {
    std::cout << "Testing 8×8 block... ";
    
    // Simple gradient pattern
    uint8_t input[64];
    for (int i = 0; i < 64; i++) {
        input[i] = (i * 4) % 256;
    }
    
    // Encode
    auto hkn = GrayscaleEncoder::encode(input, 8, 8, 75);
    
    std::cout << " [encoded: " << hkn.size() << " bytes] ";
    
    // Debug: check chunk offsets
    uint32_t chunk_count;
    std::memcpy(&chunk_count, &hkn[48], 4);
    std::cout << " [chunks: " << chunk_count << "] ";
    
    // Decode
    auto output = GrayscaleDecoder::decode(hkn);
    
    // Check size
    if (output.size() != 64) {
        std::cout << "FAIL (size mismatch: " << output.size() << " vs 64)" << std::endl;
        return;
    }
    
    // Calculate PSNR
    double psnr = calculate_psnr(input, output.data(), 64);
    
    std::cout << "PASS (PSNR: " << psnr << " dB, size: " << hkn.size() << " bytes)" << std::endl;
}

/**
 * Test 16×16 image (4 DCT blocks)
 */
void test_16x16_image() {
    std::cout << "Testing 16×16 image... ";
    
    // Checkerboard pattern
    uint8_t input[256];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            input[y * 16 + x] = ((x / 4 + y / 4) % 2) ? 200 : 50;
        }
    }
    
    // Encode
    auto hkn = GrayscaleEncoder::encode(input, 16, 16, 75);
    
    // Decode
    auto output = GrayscaleDecoder::decode(hkn);
    
    // Check size
    if (output.size() != 256) {
        std::cout << "FAIL (size mismatch)" << std::endl;
        return;
    }
    
    // Calculate PSNR
    double psnr = calculate_psnr(input, output.data(), 256);
    
    std::cout << "PASS (PSNR: " << psnr << " dB, size: " << hkn.size() << " bytes)" << std::endl;
}

/**
 * Test 32×32 image (16 DCT blocks)
 */
void test_32x32_image() {
    std::cout << "Testing 32×32 image... ";
    
    // Smooth gradient
    uint8_t input[1024];
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            input[y * 32 + x] = (x * 8) % 256;
        }
    }
    
    // Encode
    auto hkn = GrayscaleEncoder::encode(input, 32, 32, 75);
    
    // Decode
    auto output = GrayscaleDecoder::decode(hkn);
    
    // Check size
    if (output.size() != 1024) {
        std::cout << "FAIL (size mismatch)" << std::endl;
        return;
    }
    
    // Calculate PSNR
    double psnr = calculate_psnr(input, output.data(), 1024);
    
    std::cout << "PASS (PSNR: " << psnr << " dB, size: " << hkn.size() << " bytes)" << std::endl;
}

/**
 * Test quality sweep
 */
void test_quality_sweep() {
    std::cout << "\nQuality sweep (8×8 block):" << std::endl;
    
    // Simple pattern
    uint8_t input[64];
    for (int i = 0; i < 64; i++) {
        input[i] = (i * 4) % 256;
    }
    
    int qualities[] = {10, 25, 50, 75, 90, 100};
    for (int q : qualities) {
        auto hkn = GrayscaleEncoder::encode(input, 8, 8, q);
        auto output = GrayscaleDecoder::decode(hkn);
        double psnr = calculate_psnr(input, output.data(), 64);
        
        std::cout << "  Q=" << q << ": PSNR=" << psnr << " dB, size=" << hkn.size() << " bytes" << std::endl;
    }
}

/**
 * Test non-8x8-multiple dimensions (padding test)
 */
void test_padding() {
    std::cout << "Testing 13×17 image (padding)... ";
    
    // Random-ish pattern
    uint8_t input[13 * 17];
    for (int i = 0; i < 13 * 17; i++) {
        input[i] = (i * 7) % 256;
    }
    
    // Encode
    auto hkn = GrayscaleEncoder::encode(input, 13, 17, 75);
    
    // Decode
    auto output = GrayscaleDecoder::decode(hkn);
    
    // Check size
    if (output.size() != 13 * 17) {
        std::cout << "FAIL (size mismatch: " << output.size() << " vs " << (13*17) << ")" << std::endl;
        return;
    }
    
    // Calculate PSNR
    double psnr = calculate_psnr(input, output.data(), 13 * 17);
    
    std::cout << "PASS (PSNR: " << psnr << " dB)" << std::endl;
}

int main() {
    std::cout << "\n=== Phase 5 Codec Roundtrip Tests ===\n" << std::endl;
    
    test_8x8_block();
    test_16x16_image();
    test_32x32_image();
    test_padding();
    test_quality_sweep();
    
    std::cout << "\nAll roundtrip tests complete." << std::endl;
    return 0;
}
