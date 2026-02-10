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
    std::cout << "=== Phase 7a: Band-group CDF Test ===" << std::endl;
    
    const int W = 128;
    const int H = 128;
    std::vector<uint8_t> rgb(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            rgb[i+0] = (x + y) % 256;
            rgb[i+1] = x % 256;
            rgb[i+2] = y % 256;
        }
    }
    
    try {
        std::cout << "Encoding with all Phase 7a tools (AQ + 4:2:0 + CfL + BandCDF)..." << std::endl;
        auto hkn = GrayscaleEncoder::encode_color(rgb.data(), W, H, 75, true, true);
        std::cout << "Encoded Size: " << hkn.size() << " bytes" << std::endl;
        
        int out_w, out_h;
        auto decoded = GrayscaleDecoder::decode_color(hkn, out_w, out_h);
        double psnr = calc_psnr(rgb.data(), decoded.data(), W * H * 3);
        std::cout << "Final PSNR: " << psnr << " dB" << std::endl;
        
        if (psnr > 30.0) {
            std::cout << "Test PASS" << std::endl;
        } else {
            std::cout << "Test FAIL (PSNR too low: " << psnr << ")" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
