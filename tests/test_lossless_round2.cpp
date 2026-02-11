#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <random>
#include <cmath>
#include <algorithm>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include "../src/codec/headers.h"
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

static bool lossless_tile_has_filter_id(const std::vector<uint8_t>& tile_data, uint8_t filter_id) {
    if (tile_data.size() < 32) return false;
    uint32_t hdr[8] = {};
    std::memcpy(hdr, tile_data.data(), 32);
    uint32_t filter_ids_size = hdr[0];
    if (tile_data.size() < 32ull + (size_t)filter_ids_size) return false;
    const uint8_t* fids = tile_data.data() + 32;
    return std::find(fids, fids + filter_ids_size, filter_id) != (fids + filter_ids_size);
}

// ============================================================
// Test 8: MED filter should be disabled outside photo-like mode
// ============================================================
void test_med_filter_photo_gate() {
    TEST("MED filter gate (photo-only)");

    const int W = 32, H = 32;
    std::vector<int16_t> plane(W * H);
    bool found_med_case = false;

    // Search deterministic seeds until we find a block set where MED is selected
    // in photo mode. Then verify non-photo mode suppresses MED.
    for (int seed = 1; seed <= 256 && !found_med_case; seed++) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(-128, 127);
        for (auto& v : plane) v = (int16_t)dist(rng);

        auto tile_photo = GrayscaleEncoder::encode_plane_lossless(plane.data(), W, H, true);
        if (!lossless_tile_has_filter_id(tile_photo, LosslessFilter::FILTER_MED)) {
            continue;
        }

        auto tile_non_photo = GrayscaleEncoder::encode_plane_lossless(plane.data(), W, H, false);
        if (lossless_tile_has_filter_id(tile_non_photo, LosslessFilter::FILTER_MED)) {
            FAIL("MED appeared in non-photo mode for seed=" + std::to_string(seed));
            return;
        }
        found_med_case = true;
    }

    if (found_med_case) { PASS(); }
    else { FAIL("No MED-selected seed found in search range"); }
}

// ============================================================
// Test 9: TILE_MATCH4 (4x4) Roundtrip
// ============================================================
void test_tile_match4_roundtrip() {
    TEST("TILE_MATCH4 (4x4) roundtrip");

    const int W = 32, H = 32;
    const int CW = W / 4, CH = H / 4; // 4x4 cell grid
    bool found_tile4_case = false;

    for (int seed = 1; seed <= 128 && !found_tile4_case; seed++) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> byte_dist(0, 255);
        std::uniform_int_distribution<int> mode_dist(0, 3);

        // Build image in 4x4 cells with local reuse (left/up), but not fully periodic.
        std::vector<uint8_t> cells(CW * CH, 0);
        for (int cy = 0; cy < CH; cy++) {
            for (int cx = 0; cx < CW; cx++) {
                int idx = cy * CW + cx;
                int mode = mode_dist(rng);
                if (cx > 0 && mode == 0) {
                    cells[idx] = cells[idx - 1];            // left reuse
                } else if (cy > 0 && mode == 1) {
                    cells[idx] = cells[idx - CW];           // up reuse
                } else if (cx > 0 && cy > 0 && mode == 2) {
                    cells[idx] = cells[idx - CW - 1];       // up-left reuse
                } else {
                    cells[idx] = (uint8_t)byte_dist(rng);   // new value
                }
            }
        }

        std::vector<uint8_t> pixels(W * H, 0);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int cx = x / 4;
                int cy = y / 4;
                pixels[y * W + x] = cells[cy * CW + cx];
            }
        }

        auto hkn = GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
        auto stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
        if (stats.tile4_selected == 0) continue;

        auto decoded = GrayscaleDecoder::decode(hkn);
        if (decoded != pixels) {
            FAIL("TILE_MATCH4 roundtrip mismatch (seed=" + std::to_string(seed) + ")");
            return;
        }

        bool tile4_used = false;
        FileHeader hdr = FileHeader::read(hkn.data());
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* t0 = dir.find("TIL0");
        if (!t0 || t0->size < 32) {
            FAIL("Missing or invalid TIL0 chunk");
            return;
        }
        const uint8_t* td = &hkn[t0->offset];
        uint32_t th[8] = {};
        std::memcpy(th, td, 32);
        size_t bt_off = 32ull + th[0] + th[1] + th[2];
        if (bt_off + th[4] <= t0->size) {
            int nx = (int)((hdr.width + 7) / 8);
            int ny = (int)((hdr.height + 7) / 8);
            auto block_types = GrayscaleDecoder::decode_block_types(td + bt_off, th[4], nx * ny);
            tile4_used = std::count(
                block_types.begin(), block_types.end(), FileHeader::BlockType::TILE_MATCH4
            ) > 0;
        }
        if (!tile4_used) {
            FAIL("TILE_MATCH4 stats/stream mismatch (seed=" + std::to_string(seed) + ")");
            return;
        }

        found_tile4_case = true;
    }

    if (found_tile4_case) { PASS(); }
    else { FAIL("No TILE_MATCH4-selected seed found"); }
}

// ============================================================
// Test 10: Copy Mode3 Long Run Roundtrip
// ============================================================
void test_copy_mode3_long_runs() {
    TEST("Copy mode3 long-run roundtrip (2-symbol, runs of 50)");

    // 2-symbol case: alternating long runs → mode 3 should win
    // mode2 with 2 symbols = 2 + (1000*1 + 7)/8 = 127 bytes
    // mode3 = 2 + 20 tokens = 22 bytes
    std::vector<CopyParams> ops;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 50; j++) ops.push_back(CopyParams(-8, 0));  // left x50
        for (int j = 0; j < 50; j++) ops.push_back(CopyParams(0, -8));  // up x50
    }
    auto encoded = CopyCodec::encode_copy_stream(ops);

    // Verify mode 3 selected
    if (encoded.empty() || encoded[0] != 3) {
        FAIL("Expected mode 3 but got mode " + std::to_string(encoded.empty() ? -1 : (int)encoded[0]));
        return;
    }

    std::vector<CopyParams> decoded;
    CopyCodec::decode_copy_stream(encoded.data(), encoded.size(), decoded, (int)ops.size());

    if (decoded.size() != ops.size()) {
        FAIL("Decoded size " + std::to_string(decoded.size()) + " != " + std::to_string(ops.size()));
        return;
    }
    for (size_t i = 0; i < decoded.size(); i++) {
        if (!(decoded[i] == ops[i])) {
            FAIL("Mismatch at index " + std::to_string(i));
            return;
        }
    }

    // Also verify single-symbol case roundtrips correctly (mode 2 wins there, but should still work)
    std::vector<CopyParams> ops1(1000, CopyParams(-8, 0));
    auto enc1 = CopyCodec::encode_copy_stream(ops1);
    std::vector<CopyParams> dec1;
    CopyCodec::decode_copy_stream(enc1.data(), enc1.size(), dec1, 1000);
    if (dec1.size() != 1000) {
        FAIL("Single-symbol roundtrip: size " + std::to_string(dec1.size()) + " != 1000");
        return;
    }
    for (size_t i = 0; i < dec1.size(); i++) {
        if (!(dec1[i] == ops1[i])) {
            FAIL("Single-symbol mismatch at " + std::to_string(i));
            return;
        }
    }

    PASS();
}

// ============================================================
// Test 11: Copy Mode3 Mixed Runs (short/long)
// ============================================================
void test_copy_mode3_mixed_runs() {
    TEST("Copy mode3 mixed runs (short+long)");

    std::vector<CopyParams> ops;
    // Short runs: 1-3 each
    ops.push_back(CopyParams(-8, 0));  // left x1
    ops.push_back(CopyParams(0, -8));  // up x1
    ops.push_back(CopyParams(0, -8));  // up x1 (cont)
    ops.push_back(CopyParams(-8, -8)); // upleft x1

    // Long run: 64 (max single token)
    for (int i = 0; i < 64; i++) ops.push_back(CopyParams(-8, 0));

    // Another mixed
    for (int i = 0; i < 5; i++) ops.push_back(CopyParams(8, -8)); // upright x5
    for (int i = 0; i < 3; i++) ops.push_back(CopyParams(-8, 0)); // left x3

    // Exceed 64 (should split into multiple tokens)
    for (int i = 0; i < 100; i++) ops.push_back(CopyParams(0, -8));

    auto encoded = CopyCodec::encode_copy_stream(ops);
    std::vector<CopyParams> decoded;
    CopyCodec::decode_copy_stream(encoded.data(), encoded.size(), decoded, (int)ops.size());

    if (decoded.size() != ops.size()) {
        FAIL("Size: " + std::to_string(decoded.size()) + " != " + std::to_string(ops.size()));
        return;
    }
    for (size_t i = 0; i < decoded.size(); i++) {
        if (!(decoded[i] == ops[i])) {
            FAIL("Mismatch at " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 12: Copy Mode3 Malformed Payload (no crash)
// ============================================================
void test_copy_mode3_malformed() {
    TEST("Copy mode3 malformed payload (safe failure)");

    // Case 1: Truncated (mode=3, used_mask, no tokens)
    {
        uint8_t data[] = {3, 0x03}; // mode 3, used_mask=0x03, no tokens
        std::vector<CopyParams> out;
        CopyCodec::decode_copy_stream(data, 2, out, 100);
        // Should produce 100 entries (padded) or fewer, but no crash
        if (out.size() != 100) {
            FAIL("Truncated: expected 100 padded entries, got " + std::to_string(out.size()));
            return;
        }
    }

    // Case 2: Invalid symbol code (code >= used_count)
    {
        // used_mask=0x01 means only code 0 is valid, but token has code=3
        uint8_t data[] = {3, 0x01, 0xC5}; // mode 3, mask=1, token: code=3, run=6
        std::vector<CopyParams> out;
        CopyCodec::decode_copy_stream(data, 3, out, 6);
        // Should not crash (fail-safe: maps invalid code to 0)
        if (out.size() != 6) {
            FAIL("Invalid code: expected 6, got " + std::to_string(out.size()));
            return;
        }
    }

    // Case 3: mode=3 with no used_mask byte (size too small)
    {
        uint8_t data[] = {3}; // Just mode byte
        std::vector<CopyParams> out;
        CopyCodec::decode_copy_stream(data, 1, out, 50);
        // Should produce 0 entries (early return)
    }

    // Case 4: used_mask=0 (no symbols)
    {
        uint8_t data[] = {3, 0x00, 0x05};
        std::vector<CopyParams> out;
        CopyCodec::decode_copy_stream(data, 3, out, 10);
        // Should early return with 0 entries
    }

    PASS();
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
    test_med_filter_photo_gate();
    test_tile_match4_roundtrip();
    test_copy_mode3_long_runs();
    test_copy_mode3_mixed_runs();
    test_copy_mode3_malformed();

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run << " passed ===" << std::endl;
    return (tests_passed == tests_run) ? 0 : 1;
}
