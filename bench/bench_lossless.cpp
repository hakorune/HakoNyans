#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>
#include <cmath>
#include <iomanip>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;
using Clock = std::chrono::high_resolution_clock;

struct BenchResult {
    std::string name;
    int width, height;
    size_t raw_size;
    size_t hkn_size;
    double encode_ms;
    double decode_ms;
    bool exact;     // bit-exact roundtrip?
};

BenchResult bench_color_image(
    const std::string& name, const uint8_t* rgb, int w, int h, int runs = 3
) {
    BenchResult r;
    r.name = name; r.width = w; r.height = h;
    r.raw_size = w * h * 3;

    // Encode
    std::vector<uint8_t> hkn;
    double total_enc = 0;
    for (int i = 0; i < runs; i++) {
        auto t0 = Clock::now();
        hkn = GrayscaleEncoder::encode_color_lossless(rgb, w, h);
        auto t1 = Clock::now();
        total_enc += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    r.hkn_size = hkn.size();
    r.encode_ms = total_enc / runs;

    // Decode
    double total_dec = 0;
    std::vector<uint8_t> decoded;
    for (int i = 0; i < runs; i++) {
        auto t0 = Clock::now();
        int dw, dh;
        decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);
        auto t1 = Clock::now();
        total_dec += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    r.decode_ms = total_dec / runs;

    // Verify
    r.exact = (decoded.size() == r.raw_size &&
               std::memcmp(decoded.data(), rgb, r.raw_size) == 0);

    return r;
}

// Generate test images
std::vector<uint8_t> gen_random(int w, int h, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0, 255);
    std::vector<uint8_t> img(w * h * 3);
    for (auto& v : img) v = (uint8_t)d(rng);
    return img;
}

std::vector<uint8_t> gen_gradient(int w, int h) {
    std::vector<uint8_t> img(w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            img[i]   = (uint8_t)(x * 256 / w);
            img[i+1] = (uint8_t)(y * 256 / h);
            img[i+2] = (uint8_t)((x+y) * 128 / (w+h));
        }
    return img;
}

std::vector<uint8_t> gen_solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> img(w * h * 3);
    for (int i = 0; i < w*h; i++) {
        img[i*3] = r; img[i*3+1] = g; img[i*3+2] = b;
    }
    return img;
}

std::vector<uint8_t> gen_ui_screenshot(int w, int h) {
    // Simulate a UI screenshot: large flat areas + some text-like noise
    std::vector<uint8_t> img(w * h * 3);
    // Background: dark blue
    for (int i = 0; i < w*h; i++) { img[i*3]=30; img[i*3+1]=30; img[i*3+2]=60; }

    // "Title bar" - flat gray
    for (int y = 0; y < std::min(40, h); y++)
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            img[i] = 50; img[i+1] = 50; img[i+2] = 55;
        }

    // "Button" region
    for (int y = 50; y < std::min(80, h); y++)
        for (int x = 20; x < std::min(120, w); x++) {
            int i = (y * w + x) * 3;
            img[i] = 70; img[i+1] = 130; img[i+2] = 240;
        }

    // "Text" - scattered small noise
    std::mt19937 rng(12345);
    for (int y = 100; y < std::min(200, h); y++) {
        for (int x = 20; x < std::min(300, w); x++) {
            if (rng() % 5 == 0) { // 20% of pixels are "text"
                int i = (y * w + x) * 3;
                img[i] = 220; img[i+1] = 220; img[i+2] = 220;
            }
        }
    }
    return img;
}

std::vector<uint8_t> gen_natural_like(int w, int h) {
    // Low-frequency Perlin-like noise
    std::mt19937 rng(7777);
    std::uniform_int_distribution<int> base_dist(50, 200);
    std::normal_distribution<double> noise(0.0, 15.0);
    std::vector<uint8_t> img(w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            double base_r = 100 + 50 * sin(x * 0.05) * cos(y * 0.03);
            double base_g = 130 + 40 * cos(x * 0.04 + y * 0.02);
            double base_b = 80 + 60 * sin(y * 0.06);
            img[i]   = (uint8_t)std::clamp((int)(base_r + noise(rng)), 0, 255);
            img[i+1] = (uint8_t)std::clamp((int)(base_g + noise(rng)), 0, 255);
            img[i+2] = (uint8_t)std::clamp((int)(base_b + noise(rng)), 0, 255);
        }
    return img;
}

int main() {
    std::cout << "=== HakoNyans Lossless Compression Benchmark ===" << std::endl;
    std::cout << std::endl;

    struct TestImage {
        std::string name;
        int w, h;
        std::vector<uint8_t> data;
    };

    std::vector<TestImage> images;
    images.push_back({"Random 128x128",    128, 128, gen_random(128, 128)});
    images.push_back({"Random 256x256",    256, 256, gen_random(256, 256)});
    images.push_back({"Gradient 256x256",  256, 256, gen_gradient(256, 256)});
    images.push_back({"Solid 256x256",     256, 256, gen_solid(256, 256, 42, 128, 200)});
    images.push_back({"UI Screenshot 320x240", 320, 240, gen_ui_screenshot(320, 240)});
    images.push_back({"Natural-like 256x256", 256, 256, gen_natural_like(256, 256)});

    // Print table header
    std::cout << std::left
              << std::setw(28) << "Image"
              << std::right
              << std::setw(10) << "Raw(KB)"
              << std::setw(10) << "HKN(KB)"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "Enc(ms)"
              << std::setw(12) << "Dec(ms)"
              << std::setw(10) << "Exact?"
              << std::endl;
    std::cout << std::string(92, '-') << std::endl;

    bool all_exact = true;
    for (auto& img : images) {
        auto r = bench_color_image(img.name, img.data.data(), img.w, img.h);
        if (!r.exact) all_exact = false;

        std::cout << std::left
                  << std::setw(28) << r.name
                  << std::right
                  << std::setw(10) << std::fixed << std::setprecision(1) << (r.raw_size / 1024.0)
                  << std::setw(10) << std::fixed << std::setprecision(1) << (r.hkn_size / 1024.0)
                  << std::setw(10) << std::fixed << std::setprecision(3) << ((double)r.hkn_size / r.raw_size)
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.encode_ms
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.decode_ms
                  << std::setw(10) << (r.exact ? "Yes" : "NO!")
                  << std::endl;
    }

    std::cout << std::string(92, '-') << std::endl;
    std::cout << "\nAll roundtrips exact: " << (all_exact ? "YES ✓" : "NO ✗") << std::endl;
    return all_exact ? 0 : 1;
}
