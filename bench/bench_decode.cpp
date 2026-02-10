#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;

int main() {
    const int W = 1920;
    const int H = 1080;
    std::vector<uint8_t> rgb(W * H * 3);
    
    // Generate test pattern
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            rgb[i + 0] = x & 0xFF;
            rgb[i + 1] = y & 0xFF;
            rgb[i + 2] = (x + y) & 0xFF;
        }
    }
    
    std::cout << "=== HakoNyans Decode Benchmark ===" << std::endl;
    std::cout << "Resolution: " << W << "x" << H << " (Full HD)" << std::endl;
    
    // Encode once
    auto hkn = GrayscaleEncoder::encode_color(rgb.data(), W, H, 75);
    std::cout << "HKN Size: " << hkn.size() << " bytes (Ratio: " 
              << (double)hkn.size() / (W*H*3) * 100 << "%)" << std::endl;
    
    // Warm up
    int out_w, out_h;
    for (int i = 0; i < 5; i++) {
        GrayscaleDecoder::decode_color(hkn, out_w, out_h);
    }
    
    // Benchmark
    const int iterations = 20;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        auto decoded = GrayscaleDecoder::decode_color(hkn, out_w, out_h);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    auto avg_ms = duration / iterations;
    auto throughput = (double)(W * H * 3) / (avg_ms / 1000.0) / (1024 * 1024);
    
    std::cout << "Average Decode Time: " << avg_ms << " ms" << std::endl;
    std::cout << "Throughput:         " << throughput << " MiB/s" << std::endl;
    
    if (throughput > 100.0) {
        std::cout << "\nTarget >100 MiB/s ACHIEVED!" << std::endl;
    } else {
        std::cout << "\nTarget >100 MiB/s not reached yet." << std::endl;
    }
    
    return 0;
}