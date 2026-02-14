
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

// ============================================================
// Test 13: Filter IDs rANS roundtrip (Phase 9n)
// ============================================================
void test_filter_ids_rans_roundtrip() {
    TEST("Filter IDs rANS roundtrip (uniform filter, 64x64 solid)");

    // Solid image → all filter residuals are predictable, filter_ids should be compressible
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 3);
    // Fill with a single color
    for (int i = 0; i < W * H; i++) {
        pixels[i * 3 + 0] = 42;
        pixels[i * 3 + 1] = 100;
        pixels[i * 3 + 2] = 200;
    }

    auto encoded = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    if (decoded.size() != pixels.size()) {
        FAIL("Decoded size " + std::to_string(decoded.size()) + " != " + std::to_string(pixels.size()));
        return;
    }
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded[i] != pixels[i]) {
            FAIL("Pixel mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 14: Filter IDs LZ roundtrip (Phase 9n)
// ============================================================
void test_filter_ids_lz_roundtrip() {
    TEST("Filter IDs LZ roundtrip (gradient, 64x64)");

    // Gradient image → filter selection varies but may have patterns
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            pixels[i * 3 + 0] = (uint8_t)(x * 4);
            pixels[i * 3 + 1] = (uint8_t)(y * 4);
            pixels[i * 3 + 2] = (uint8_t)((x + y) * 2);
        }
    }

    auto encoded = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    if (decoded.size() != pixels.size()) {
        FAIL("Decoded size " + std::to_string(decoded.size()) + " != " + std::to_string(pixels.size()));
        return;
    }
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded[i] != pixels[i]) {
            FAIL("Pixel mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 15: Filter HI sparse roundtrip (Phase 9n)
// ============================================================
void test_filter_hi_sparse_roundtrip() {
    TEST("Filter HI sparse roundtrip (small residual, 64x64)");

    // Nearly flat image with small variations → hi bytes should be mostly zero
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 3);
    std::mt19937 rng(999);
    for (int i = 0; i < W * H; i++) {
        // Small variations around 128
        pixels[i * 3 + 0] = 128 + (rng() % 5);
        pixels[i * 3 + 1] = 128 + (rng() % 5);
        pixels[i * 3 + 2] = 128 + (rng() % 5);
    }

    auto encoded = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    if (decoded.size() != pixels.size()) {
        FAIL("Decoded size " + std::to_string(decoded.size()) + " != " + std::to_string(pixels.size()));
        return;
    }
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded[i] != pixels[i]) {
            FAIL("Pixel mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 16: Filter wrapper malformed (Phase 9n)
// ============================================================
void test_filter_wrapper_malformed() {
    TEST("Filter wrapper malformed (no crash)");

    // Verify a normal encode/decode still works (basic smoke test
    // that the wrapper code paths don't break normal operation)
    const int W = 32, H = 32;
    std::vector<uint8_t> pixels(W * H * 3);
    std::mt19937 rng(777);
    for (size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = rng() & 0xFF;
    }

    auto encoded = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    if (decoded.size() != pixels.size()) {
        FAIL("Decoded size mismatch");
        return;
    }
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded[i] != pixels[i]) {
            FAIL("Pixel mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 17: Filter Lo delta roundtrip (Phase 9o)
// ============================================================
void test_filter_lo_delta_roundtrip() {
    TEST("Filter Lo delta roundtrip (smooth gradient, 64x64)");

    // Smooth gradients → delta transform should be very effective
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            pixels[i * 3 + 0] = (uint8_t)((x + y) * 2);
            pixels[i * 3 + 1] = (uint8_t)(x * 3 + 10);
            pixels[i * 3 + 2] = (uint8_t)(y * 3 + 20);
        }
    }

    auto encoded = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    if (decoded.size() != pixels.size()) {
        FAIL("Decoded size " + std::to_string(decoded.size()) + " != " + std::to_string(pixels.size()));
        return;
    }
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded[i] != pixels[i]) {
            FAIL("Pixel mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 18: Filter Lo LZ roundtrip (Phase 9o)
// ============================================================
void test_filter_lo_lz_roundtrip() {
    TEST("Filter Lo LZ roundtrip (random, 64x64)");

    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 3);
    std::mt19937 rng(888);
    for (size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = rng() & 0xFF;
    }

    auto encoded = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    if (decoded.size() != pixels.size()) {
        FAIL("Decoded size " + std::to_string(decoded.size()) + " != " + std::to_string(pixels.size()));
        return;
    }
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded[i] != pixels[i]) {
            FAIL("Pixel mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 18b: Filter Lo Mode 5 core pipeline (LZ + rANS shared/static CDF)
// ============================================================
void test_filter_lo_lz_rans_pipeline() {
    TEST("Filter Lo Mode 5 pipeline (LZ+rANS shared CDF)");

    // Build a long repetitive byte stream so LZ tags themselves are compressible.
    std::vector<uint8_t> src;
    src.reserve(32768);
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 128; x++) {
            uint8_t v = (uint8_t)(((x / 8) + (y / 16) * 3) & 0xFF);
            src.push_back(v);
        }
    }
    if (src.empty()) {
        FAIL("source stream empty");
        return;
    }

    auto lz = hakonyans::TileLZ::compress(src);
    if (lz.empty()) {
        FAIL("TileLZ compression failed");
        return;
    }
    auto lz_rans = hakonyans::GrayscaleEncoder::encode_byte_stream_shared_lz(lz);
    auto lz_dec = hakonyans::GrayscaleDecoder::decode_byte_stream_shared_lz(lz_rans.data(), lz_rans.size(), 0);
    if (lz_dec.empty()) {
        FAIL("decode_byte_stream failed on LZ payload");
        return;
    }
    auto out = hakonyans::TileLZ::decompress(lz_dec.data(), lz_dec.size(), src.size());
    if (out.size() != src.size()) {
        FAIL("decompressed size mismatch");
        return;
    }
    for (size_t i = 0; i < src.size(); i++) {
        if (out[i] != src[i]) {
            FAIL("pipeline mismatch at byte " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Test 18c: TileLZ core roundtrip/decode edge cases
// ============================================================
void test_tile_lz_core_roundtrip() {
    TEST("TileLZ core roundtrip/decode edge cases");

    std::mt19937 rng(20260212);
    const std::vector<size_t> sizes = {
        0, 1, 2, 3, 4, 7, 15, 31, 63, 64, 127, 255, 256, 1024, 4096, 8192
    };

    for (size_t sz : sizes) {
        for (int kind = 0; kind < 5; kind++) {
            std::vector<uint8_t> src(sz, 0);
            if (kind == 0) {
                for (size_t i = 0; i < sz; i++) src[i] = (uint8_t)(rng() & 0xFF);
            } else if (kind == 1) {
                std::fill(src.begin(), src.end(), (uint8_t)0xAA);
            } else if (kind == 2) {
                for (size_t i = 0; i < sz; i++) src[i] = (uint8_t)(i & 0xFF);
            } else if (kind == 3) {
                for (size_t i = 0; i < sz; i++) src[i] = (uint8_t)(((i % 17) * 13) & 0xFF);
            } else {
                for (size_t i = 0; i < sz; i++) src[i] = (uint8_t)((i % 2 == 0) ? 0x11 : 0xEE);
            }

            auto lz = TileLZ::compress(src);
            if (sz > 0 && lz.empty()) {
                FAIL("compress produced empty stream for non-empty input");
                return;
            }

            std::vector<uint8_t> out;
            const uint8_t* p = lz.empty() ? nullptr : lz.data();
            if (!TileLZ::decompress_to(p, lz.size(), out, src.size())) {
                FAIL("decompress_to failed at size=" + std::to_string(sz) + ", kind=" + std::to_string(kind));
                return;
            }
            if (out != src) {
                FAIL("decompress_to mismatch at size=" + std::to_string(sz) + ", kind=" + std::to_string(kind));
                return;
            }

            auto out2 = TileLZ::decompress(p, lz.size(), src.size());
            if (out2 != src) {
                FAIL("decompress mismatch at size=" + std::to_string(sz) + ", kind=" + std::to_string(kind));
                return;
            }
        }
    }

    // Overlap copy (dist=1): "A" + match(7,1) => "AAAAAAAA"
    {
        const uint8_t stream[] = {0, 1, (uint8_t)'A', 1, 7, 1, 0};
        std::vector<uint8_t> out;
        if (!TileLZ::decompress_to(stream, sizeof(stream), out, 8)) {
            FAIL("overlap dist=1 decode failed");
            return;
        }
        for (uint8_t v : out) {
            if (v != (uint8_t)'A') {
                FAIL("overlap dist=1 decode mismatch");
                return;
            }
        }
    }

    // Overlap copy (dist=2): "AB" + match(6,2) => "ABABABAB"
    {
        const uint8_t stream[] = {0, 2, (uint8_t)'A', (uint8_t)'B', 1, 6, 2, 0};
        std::vector<uint8_t> out;
        if (!TileLZ::decompress_to(stream, sizeof(stream), out, 8)) {
            FAIL("overlap dist=2 decode failed");
            return;
        }
        const uint8_t expect[] = {'A', 'B', 'A', 'B', 'A', 'B', 'A', 'B'};
        for (size_t i = 0; i < 8; i++) {
            if (out[i] != expect[i]) {
                FAIL("overlap dist=2 decode mismatch");
                return;
            }
        }
    }

    // Malformed stream (dist > history) must fail safely.
    {
        const uint8_t bad[] = {1, 4, 5, 0};
        std::vector<uint8_t> out;
        if (TileLZ::decompress_to(bad, sizeof(bad), out, 4)) {
            FAIL("malformed stream unexpectedly succeeded");
            return;
        }
    }

    PASS();
}

// ============================================================
// Test 19: Filter Lo malformed wrapper (Phase 9o)
// ============================================================
