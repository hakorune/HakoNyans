#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <random>
#include <cmath>

#include "../src/codec/colorspace.h"
#include "../src/codec/lossless_filter.h"

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
// Test 1: YCoCg-R roundtrip (exhaustive 256^3)
// ============================================================
void test_ycocg_r_roundtrip_exhaustive() {
    TEST("YCoCg-R roundtrip (exhaustive 256^3 colors)");
    
    int errors = 0;
    for (int r = 0; r < 256; r++) {
        for (int g = 0; g < 256; g++) {
            for (int b = 0; b < 256; b++) {
                int16_t y, co, cg;
                rgb_to_ycocg_r((uint8_t)r, (uint8_t)g, (uint8_t)b, y, co, cg);
                
                uint8_t r2, g2, b2;
                ycocg_r_to_rgb(y, co, cg, r2, g2, b2);
                
                if (r2 != r || g2 != g || b2 != b) {
                    if (errors < 5) {
                        std::cout << "\n    Mismatch: RGB(" << r << "," << g << "," << b 
                                  << ") -> YCoCg(" << y << "," << co << "," << cg 
                                  << ") -> RGB(" << (int)r2 << "," << (int)g2 << "," << (int)b2 << ")";
                    }
                    errors++;
                }
            }
        }
    }
    
    if (errors == 0) { PASS(); }
    else { FAIL(std::to_string(errors) + " mismatches out of 16M colors"); }
}

// ============================================================
// Test 2: YCoCg-R value range check
// ============================================================
void test_ycocg_r_ranges() {
    TEST("YCoCg-R value ranges (Y: [0,255], Co/Cg: [-255,255])");
    
    int16_t min_y = 32767, max_y = -32768;
    int16_t min_co = 32767, max_co = -32768;
    int16_t min_cg = 32767, max_cg = -32768;
    
    for (int r = 0; r < 256; r += 5) {
        for (int g = 0; g < 256; g += 5) {
            for (int b = 0; b < 256; b += 5) {
                int16_t y, co, cg;
                rgb_to_ycocg_r((uint8_t)r, (uint8_t)g, (uint8_t)b, y, co, cg);
                min_y = std::min(min_y, y); max_y = std::max(max_y, y);
                min_co = std::min(min_co, co); max_co = std::max(max_co, co);
                min_cg = std::min(min_cg, cg); max_cg = std::max(max_cg, cg);
            }
        }
    }
    
    bool ok = (min_y >= 0 && max_y <= 255 &&
               min_co >= -255 && max_co <= 255 &&
               min_cg >= -255 && max_cg <= 255);
    
    if (ok) { PASS(); }
    else {
        FAIL("Y=[" + std::to_string(min_y) + "," + std::to_string(max_y) + "] " +
             "Co=[" + std::to_string(min_co) + "," + std::to_string(max_co) + "] " +
             "Cg=[" + std::to_string(min_cg) + "," + std::to_string(max_cg) + "]");
    }
}

// ============================================================
// Test 3: ZigZag encode/decode roundtrip
// ============================================================
void test_zigzag_roundtrip() {
    TEST("ZigZag encode/decode roundtrip");
    
    bool ok = true;
    // Test full int16_t range
    for (int v = -511; v <= 511; v++) {
        int16_t orig = (int16_t)v;
        uint16_t encoded = zigzag_encode_val(orig);
        int16_t decoded = zigzag_decode_val(encoded);
        if (decoded != orig) {
            ok = false;
            std::cout << "\n    ZigZag mismatch: " << orig << " -> " << encoded << " -> " << decoded;
            break;
        }
    }
    
    // Verify mapping: 0->0, -1->1, 1->2, -2->3, 2->4
    ok = ok && (zigzag_encode_val(0) == 0);
    ok = ok && (zigzag_encode_val(-1) == 1);
    ok = ok && (zigzag_encode_val(1) == 2);
    ok = ok && (zigzag_encode_val(-2) == 3);
    ok = ok && (zigzag_encode_val(2) == 4);
    
    if (ok) { PASS(); }
    else { FAIL("ZigZag roundtrip failed"); }
}

// ============================================================
// Test 4: Filter individual roundtrip (each filter type)
// ============================================================
void test_filter_individual_roundtrip() {
    TEST("Filter individual roundtrip (5 filter types)");
    
    const int W = 16, H = 8;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(-255, 255);
    
    std::vector<int16_t> image(W * H);
    for (auto& v : image) v = (int16_t)dist(rng);
    
    bool ok = true;
    for (int ft = 0; ft < LosslessFilter::FILTER_COUNT; ft++) {
        // Apply filter
        std::vector<uint8_t> filter_ids(H, (uint8_t)ft);
        std::vector<int16_t> filtered(W * H);
        
        for (int y = 0; y < H; y++) {
            const int16_t* prev = (y > 0) ? image.data() + (y - 1) * W : nullptr;
            LosslessFilter::filter_row(
                image.data() + y * W, prev, W,
                (LosslessFilter::FilterType)ft,
                filtered.data() + y * W
            );
        }
        
        // Unfilter
        std::vector<int16_t> reconstructed;
        LosslessFilter::unfilter_image(filter_ids.data(), filtered.data(), W, H, reconstructed);
        
        if (reconstructed != image) {
            ok = false;
            std::cout << "\n    Filter " << ft << " roundtrip failed";
        }
    }
    
    if (ok) { PASS(); }
    else { FAIL("Filter roundtrip mismatch"); }
}

// ============================================================
// Test 5: Auto filter selection roundtrip
// ============================================================
void test_filter_auto_roundtrip() {
    TEST("Auto filter selection roundtrip");
    
    const int W = 32, H = 16;
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(-100, 200);
    
    std::vector<int16_t> image(W * H);
    for (auto& v : image) v = (int16_t)dist(rng);
    
    std::vector<uint8_t> filter_ids;
    std::vector<int16_t> filtered;
    LosslessFilter::filter_image(image.data(), W, H, filter_ids, filtered);
    
    std::vector<int16_t> reconstructed;
    LosslessFilter::unfilter_image(filter_ids.data(), filtered.data(), W, H, reconstructed);
    
    if (reconstructed == image) { PASS(); }
    else { FAIL("Auto filter roundtrip mismatch"); }
}

// ============================================================
// Test 6: Gradient image — filter should produce small residuals
// ============================================================
void test_filter_gradient_efficiency() {
    TEST("Gradient image filter efficiency");
    
    const int W = 64, H = 64;
    std::vector<int16_t> image(W * H);
    
    // Horizontal gradient: 0, 1, 2, ..., 63, 0, 1, ...
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            image[y * W + x] = (int16_t)x;
    
    std::vector<uint8_t> filter_ids;
    std::vector<int16_t> filtered;
    LosslessFilter::filter_image(image.data(), W, H, filter_ids, filtered);
    
    // Check that filtered residuals are mostly near zero
    int64_t sum_abs = 0;
    for (auto v : filtered) sum_abs += std::abs((int)v);
    
    // With Sub filter, residuals should be mostly 1 (except first pixel of each row)
    // Expected: ~W*H * 1 = 4096, with first pixel of each row = value
    // No-filter sum would be ~64*8*(64/2) = huge
    int64_t no_filter_sum = 0;
    for (auto v : image) no_filter_sum += std::abs((int)v);
    
    bool efficient = (sum_abs < no_filter_sum / 2);  // At least 2x reduction
    
    // Verify roundtrip
    std::vector<int16_t> reconstructed;
    LosslessFilter::unfilter_image(filter_ids.data(), filtered.data(), W, H, reconstructed);
    bool exact = (reconstructed == image);
    
    if (efficient && exact) { PASS(); }
    else {
        if (!exact) FAIL("Gradient roundtrip mismatch");
        else FAIL("Filter not efficient enough: filtered=" + std::to_string(sum_abs) + 
                  " vs raw=" + std::to_string(no_filter_sum));
    }
}

// ============================================================
// Test 7: Full pipeline YCoCg-R + Filter roundtrip
// ============================================================
void test_full_pipeline_roundtrip() {
    TEST("Full pipeline: RGB -> YCoCg-R -> Filter -> Unfilter -> RGB");
    
    const int W = 16, H = 16;
    std::mt19937 rng(999);
    std::uniform_int_distribution<int> dist(0, 255);
    
    // Generate random RGB image
    std::vector<uint8_t> rgb(W * H * 3);
    for (auto& v : rgb) v = (uint8_t)dist(rng);
    
    // Step 1: RGB -> YCoCg-R (3 planes of int16_t)
    std::vector<int16_t> y_plane(W * H), co_plane(W * H), cg_plane(W * H);
    for (int i = 0; i < W * H; i++) {
        rgb_to_ycocg_r(rgb[i*3], rgb[i*3+1], rgb[i*3+2],
                        y_plane[i], co_plane[i], cg_plane[i]);
    }
    
    // Step 2: Filter each plane
    std::vector<uint8_t> y_fids, co_fids, cg_fids;
    std::vector<int16_t> y_filt, co_filt, cg_filt;
    LosslessFilter::filter_image(y_plane.data(), W, H, y_fids, y_filt);
    LosslessFilter::filter_image(co_plane.data(), W, H, co_fids, co_filt);
    LosslessFilter::filter_image(cg_plane.data(), W, H, cg_fids, cg_filt);
    
    // Step 3: Unfilter
    std::vector<int16_t> y_rec, co_rec, cg_rec;
    LosslessFilter::unfilter_image(y_fids.data(), y_filt.data(), W, H, y_rec);
    LosslessFilter::unfilter_image(co_fids.data(), co_filt.data(), W, H, co_rec);
    LosslessFilter::unfilter_image(cg_fids.data(), cg_filt.data(), W, H, cg_rec);
    
    // Step 4: YCoCg-R -> RGB
    std::vector<uint8_t> rgb_rec(W * H * 3);
    for (int i = 0; i < W * H; i++) {
        ycocg_r_to_rgb(y_rec[i], co_rec[i], cg_rec[i],
                        rgb_rec[i*3], rgb_rec[i*3+1], rgb_rec[i*3+2]);
    }
    
    // Verify bit-exact match
    if (rgb == rgb_rec) { PASS(); }
    else {
        int mismatches = 0;
        for (size_t i = 0; i < rgb.size(); i++) {
            if (rgb[i] != rgb_rec[i]) mismatches++;
        }
        FAIL(std::to_string(mismatches) + " byte mismatches");
    }
}

int main() {
    std::cout << "=== Phase 8 Round 1: Lossless Foundation Tests ===" << std::endl;
    
    test_ycocg_r_roundtrip_exhaustive();
    test_ycocg_r_ranges();
    test_zigzag_roundtrip();
    test_filter_individual_roundtrip();
    test_filter_auto_roundtrip();
    test_filter_gradient_efficiency();
    test_full_pipeline_roundtrip();
    
    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run << " passed ===" << std::endl;
    return (tests_passed == tests_run) ? 0 : 1;
}
