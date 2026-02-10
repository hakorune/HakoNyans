#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <random>
#include <cmath>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  Test " << tests_run << ": " << name << " ... "; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "PASS" << std::endl; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAIL: " << msg << std::endl; \
    } while(0)

// ============================================================
// Test 1: Grayscale Lossless Roundtrip (bit-exact)
// ============================================================
void test_gray_lossless() {
    TEST("Grayscale lossless roundtrip (32x32)");

    const int W = 32, H = 32;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<uint8_t> pixels(W * H);
    for (auto& v : pixels) v = (uint8_t)dist(rng);

    auto hkn = GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
    auto decoded = GrayscaleDecoder::decode(hkn);

    if (decoded == pixels) { PASS(); }
    else {
        int mismatches = 0;
        for (size_t i = 0; i < pixels.size(); i++) {
            if (pixels[i] != decoded[i]) mismatches++;
        }
        FAIL(std::to_string(mismatches) + " byte mismatches out of " + std::to_string(pixels.size()));
    }
}

// ============================================================
// Test 2: Color Lossless Roundtrip (bit-exact)
// ============================================================
void test_color_lossless() {
    TEST("Color lossless roundtrip (32x32)");

    const int W = 32, H = 32;
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<uint8_t> rgb(W * H * 3);
    for (auto& v : rgb) v = (uint8_t)dist(rng);

    auto hkn = GrayscaleEncoder::encode_color_lossless(rgb.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);

    bool size_ok = (dw == W && dh == H);
    bool data_ok = (decoded == rgb);

    if (size_ok && data_ok) { PASS(); }
    else {
        if (!size_ok) {
            FAIL("Size mismatch: " + std::to_string(dw) + "x" + std::to_string(dh) +
                 " vs " + std::to_string(W) + "x" + std::to_string(H));
        } else {
            int mismatches = 0;
            for (size_t i = 0; i < rgb.size(); i++) {
                if (rgb[i] != decoded[i]) mismatches++;
            }
            FAIL(std::to_string(mismatches) + " byte mismatches out of " + std::to_string(rgb.size()));
        }
    }
}

// ============================================================
// Test 3: Gradient Image Lossless
// ============================================================
void test_gradient_lossless() {
    TEST("Gradient image lossless (64x64)");

    const int W = 64, H = 64;
    std::vector<uint8_t> rgb(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 3;
            rgb[idx]     = (uint8_t)(x * 4);  // R gradient
            rgb[idx + 1] = (uint8_t)(y * 4);  // G gradient
            rgb[idx + 2] = (uint8_t)((x + y) * 2);  // B gradient
        }
    }

    auto hkn = GrayscaleEncoder::encode_color_lossless(rgb.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);

    if (decoded == rgb) { PASS(); }
    else { FAIL("Gradient roundtrip mismatch"); }
}

// ============================================================
// Test 4: Random Large Image Lossless
// ============================================================
void test_large_random_lossless() {
    TEST("Large random image lossless (128x128)");

    const int W = 128, H = 128;
    std::mt19937 rng(999);
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<uint8_t> rgb(W * H * 3);
    for (auto& v : rgb) v = (uint8_t)dist(rng);

    auto hkn = GrayscaleEncoder::encode_color_lossless(rgb.data(), W, H);

    // Report compression ratio
    size_t raw_size = W * H * 3;
    size_t hkn_size = hkn.size();
    double ratio = (double)hkn_size / raw_size;

    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);

    if (decoded == rgb) {
        std::cout << "PASS (ratio=" << std::fixed;
        std::cout.precision(2);
        std::cout << ratio << ", " << hkn_size << "/" << raw_size << " bytes)" << std::endl;
        tests_passed++;
    } else {
        FAIL("Random large image roundtrip mismatch");
    }
}

// ============================================================
// Test 5: Non-power-of-2 dimensions
// ============================================================
void test_odd_dimensions() {
    TEST("Odd dimensions lossless (7x9, 1x1, 13x5)");

    struct TestCase { int w, h; };
    TestCase cases[] = {{7, 9}, {1, 1}, {13, 5}, {3, 100}, {100, 3}};
    bool ok = true;

    for (const auto& tc : cases) {
        std::mt19937 rng(tc.w * 100 + tc.h);
        std::uniform_int_distribution<int> dist(0, 255);

        std::vector<uint8_t> rgb(tc.w * tc.h * 3);
        for (auto& v : rgb) v = (uint8_t)dist(rng);

        auto hkn = GrayscaleEncoder::encode_color_lossless(rgb.data(), tc.w, tc.h);
        int dw, dh;
        auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);

        if (decoded != rgb || dw != tc.w || dh != tc.h) {
            ok = false;
            std::cout << "\n    Failed for " << tc.w << "x" << tc.h;
        }
    }

    if (ok) { PASS(); }
    else { FAIL("Odd dimensions mismatch"); }
}

// ============================================================
// Test 6: Flat (solid color) image
// ============================================================
void test_flat_image() {
    TEST("Flat solid color image lossless (64x64)");

    const int W = 64, H = 64;
    std::vector<uint8_t> rgb(W * H * 3);
    // Fill with a single color
    for (int i = 0; i < W * H; i++) {
        rgb[i * 3]     = 42;
        rgb[i * 3 + 1] = 128;
        rgb[i * 3 + 2] = 200;
    }

    auto hkn = GrayscaleEncoder::encode_color_lossless(rgb.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);

    size_t raw_size = W * H * 3;
    double ratio = (double)hkn.size() / raw_size;

    if (decoded == rgb) {
        std::cout << "PASS (ratio=" << std::fixed;
        std::cout.precision(3);
        std::cout << ratio << ")" << std::endl;
        tests_passed++;
    } else {
        FAIL("Flat image mismatch");
    }
}

// ============================================================
// Test 7: Header flags check
// ============================================================
void test_header_flags() {
    TEST("Header: flags=lossless, quality=0, colorspace=YCoCg-R");

    const int W = 16, H = 16;
    std::vector<uint8_t> rgb(W * H * 3, 128);
    auto hkn = GrayscaleEncoder::encode_color_lossless(rgb.data(), W, H);

    FileHeader hdr = FileHeader::read(hkn.data());
    bool ok = true;
    if (!(hdr.flags & 1))    { ok = false; std::cout << "\n    flags bit0 not set"; }
    if (hdr.quality != 0)    { ok = false; std::cout << "\n    quality=" << (int)hdr.quality << " (expected 0)"; }
    if (hdr.colorspace != 1) { ok = false; std::cout << "\n    colorspace=" << (int)hdr.colorspace << " (expected 1=YCoCg-R)"; }
    if (hdr.num_channels != 3) { ok = false; std::cout << "\n    channels=" << (int)hdr.num_channels; }

    if (ok) { PASS(); }
    else { FAIL("Header flags incorrect"); }
}

int main() {
    std::cout << "=== Phase 8 Round 2: Lossless Codec Tests ===" << std::endl;

    test_gray_lossless();
    test_color_lossless();
    test_gradient_lossless();
    test_large_random_lossless();
    test_odd_dimensions();
    test_flat_image();
    test_header_flags();

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run << " passed ===" << std::endl;
    return (tests_passed == tests_run) ? 0 : 1;
}
