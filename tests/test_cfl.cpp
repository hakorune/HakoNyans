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
    std::cout << "=== Phase 7a: CfL (Chroma from Luma) Test ===" << std::endl;
    
    const int W = 64;
    const int H = 64;
    std::vector<uint8_t> rgb(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            uint8_t luma = x * 4;
            rgb[i+0] = luma;
            rgb[i+1] = luma / 2;
            rgb[i+2] = luma / 4;
        }
    }
    
    try {
        std::cout << "Testing 4:2:0 without CfL..." << std::endl;
        auto hkn_no_cfl = GrayscaleEncoder::encode_color(rgb.data(), W, H, 75, true, false);
        int out_w, out_h;
        auto dec_no_cfl = GrayscaleDecoder::decode_color(hkn_no_cfl, out_w, out_h);
        double psnr_no_cfl = calc_psnr(rgb.data(), dec_no_cfl.data(), W * H * 3);
        std::cout << "No-CfL Size: " << hkn_no_cfl.size() << " bytes, PSNR: " << psnr_no_cfl << " dB" << std::endl;
        
        std::cout << "\nTesting 4:2:0 with CfL..." << std::endl;
        auto hkn_cfl = GrayscaleEncoder::encode_color(rgb.data(), W, H, 75, true, true);
        auto dec_cfl = GrayscaleDecoder::decode_color(hkn_cfl, out_w, out_h);
        double psnr_cfl = calc_psnr(rgb.data(), dec_cfl.data(), W * H * 3);
        std::cout << "CfL Size: " << hkn_cfl.size() << " bytes, PSNR: " << psnr_cfl << " dB" << std::endl;
        
        if (psnr_cfl > 30.0) {
            std::cout << "Test PASS" << std::endl;
        } else {
            std::cout << "Test FAIL (PSNR too low)" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}