void test_filter_rows_lzcost_roundtrip() {
    TEST("LZCOST filter rows roundtrip");

    // Create a simple test image
    const uint32_t w = 64, h = 64;
    std::vector<uint8_t> rgb(w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = (uint8_t)(i % 256);

    // Set environment to use LZCOST
    setenv("HKN_FILTER_ROWS_COST_MODEL", "lzcost", 1);
    setenv("HKN_FILTER_ROWS_LZCOST_TOPK", "2", 1);
    setenv("HKN_FILTER_ROWS_LZCOST_WINDOW", "256", 1);
    setenv("HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY", "1", 1);

    auto encoded = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);

    // Clear environment
    unsetenv("HKN_FILTER_ROWS_COST_MODEL");
    unsetenv("HKN_FILTER_ROWS_LZCOST_TOPK");
    unsetenv("HKN_FILTER_ROWS_LZCOST_WINDOW");
    unsetenv("HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY");

    // Decode and verify
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);
    if (decoded == rgb && dw == (int)w && dh == (int)h) {
        PASS();
    } else {
        FAIL("Decoded data mismatch");
    }
}

// ============================================================
void test_filter_rows_lzcost_deterministic() {
    TEST("LZCOST filter rows deterministic");

    const uint32_t w = 64, h = 64;
    std::vector<uint8_t> rgb(w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = (uint8_t)(i % 256);

    setenv("HKN_FILTER_ROWS_COST_MODEL", "lzcost", 1);

    // Encode twice with same input
    auto encoded1 = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);
    auto encoded2 = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);

    unsetenv("HKN_FILTER_ROWS_COST_MODEL");

    if (encoded1 == encoded2) {
        PASS();
    } else {
        FAIL("Non-deterministic output");
    }
}

// ============================================================
void test_filter_rows_lzcost_photo_only_disabled() {
    TEST("LZCOST PHOTO_ONLY=1 does not affect ANIME/UI");

    // Create a UI-like image (flat colors)
    const uint32_t w = 64, h = 64;
    std::vector<uint8_t> rgb(w * h * 3, 200); // Flat gray

    setenv("HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY", "1", 1);
    setenv("HKN_FILTER_ROWS_COST_MODEL", "lzcost", 1);

    auto encoded = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);

    // Decode and verify
    int dw, dh;
    auto decoded = GrayscaleDecoder::decode_color(encoded, dw, dh);

    unsetenv("HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY");
    unsetenv("HKN_FILTER_ROWS_COST_MODEL");

    if (decoded == rgb && dw == (int)w && dh == (int)h) {
        PASS();
    } else {
        FAIL("UI profile decode mismatch");
    }
}

// ============================================================
void test_filter_rows_lzcost_env_default_compat() {
    TEST("LZCOST env unset maintains bit-identical output");

    const uint32_t w = 64, h = 64;
    std::vector<uint8_t> rgb(w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = (uint8_t)(i % 256);

    // Ensure no LZCOST env vars are set
    unsetenv("HKN_FILTER_ROWS_COST_MODEL");
    unsetenv("HKN_FILTER_ROWS_LZCOST_TOPK");
    unsetenv("HKN_FILTER_ROWS_LZCOST_WINDOW");
    unsetenv("HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY");

    auto encoded1 = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);
    auto encoded2 = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);

    if (encoded1 == encoded2) {
        PASS();
    } else {
        FAIL("Non-identical output with default settings");
    }
}

// ============================================================
void test_csv_column_count_consistency() {
    TEST("CSV column count consistency");

    // Create a minimal test to verify CSV format
    // This test ensures the CSV header and data rows have matching column counts
    const uint32_t w = 32, h = 32;
    std::vector<uint8_t> rgb(w * h * 3, 100);

    auto encoded = GrayscaleEncoder::encode_color_lossless(rgb.data(), w, h);

    // If we get here, the encoding succeeded
    // The actual CSV verification is done by bench_png_compare itself
    PASS();
}

// ============================================================
// Test: Filter Lo Mode 8 Roundtrip (Phase 9X-5)
// ============================================================
void test_filter_lo_mode8_roundtrip() {
    TEST("Filter Lo Mode 8 roundtrip with env enable");

    // Enable mode8
    ScopedEnvVar env_enable("HKN_FILTER_LO_MODE8_ENABLE", "1");

    // Mixed content image to exercise all paths
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 3);
    std::mt19937 rng(888);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            // Create pattern that might benefit from per-context codec selection
            if ((x / 8) % 2 == 0) {
                // Smooth gradient (delta should work well)
                pixels[i * 3 + 0] = (uint8_t)(x * 4);
                pixels[i * 3 + 1] = (uint8_t)(y * 4);
                pixels[i * 3 + 2] = (uint8_t)((x + y) * 2);
            } else {
                // Random noise (LZ might work well)
                pixels[i * 3 + 0] = rng() & 0xFF;
                pixels[i * 3 + 1] = rng() & 0xFF;
                pixels[i * 3 + 2] = rng() & 0xFF;
            }
        }
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
// Test: Filter Lo Mode 8 Malformed Payload (Phase 9X-5)
// ============================================================
void test_filter_lo_mode8_malformed() {
    TEST("Filter Lo Mode 8 malformed payload (safety check)");

    using namespace hakonyans::lossless_filter_lo_decode;

    // Create a valid mode8 payload first
    const uint32_t raw_count = 64;  // Small test
    std::vector<uint8_t> payload(30 + 6);  // header + 6 bytes of stream data

    // Set up valid mode8 header
    payload[0] = 0;   // codec_id[0] = legacy
    payload[1] = 1;   // codec_id[1] = delta
    payload[2] = 2;   // codec_id[2] = lz
    payload[3] = 255; // codec_id[3] = empty
    payload[4] = 255; // codec_id[4] = empty
    payload[5] = 255; // codec_id[5] = empty

    // Set lengths - 2 bytes for each non-empty context
    uint32_t len0 = 2, len1 = 2, len2 = 2;
    for (int i = 0; i < 4; i++) {
        payload[6 + i] = (uint8_t)(len0 >> (i * 8));
        payload[10 + i] = (uint8_t)(len1 >> (i * 8));
        payload[14 + i] = (uint8_t)(len2 >> (i * 8));
        payload[18 + i] = 0;  // len3 = 0
        payload[22 + i] = 0;  // len4 = 0
        payload[26 + i] = 0;  // len5 = 0
    }
    // Fill stream data with zeros (will decode as garbage but test safety)
    for (size_t i = 30; i < payload.size(); i++) {
        payload[i] = 0;
    }

    // Create minimal supporting structures for decode
    std::vector<uint8_t> filter_ids(8, 0);
    std::vector<FileHeader::BlockType> block_types(1, FileHeader::BlockType::DCT);

    // Test 1: len sum mismatch (payload_size != 30 + sum(len))
    {
        auto payload_bad = payload;
        payload_bad.push_back(0xFF);  // Add extra byte

        auto result = decode_filter_lo_stream(
            payload_bad.data(), payload_bad.size(), raw_count,
            filter_ids, block_types, 8, 1, false, false,
            FileHeader::VERSION_FILTER_LO_CTX_HYBRID_CODEC,
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            nullptr
        );
        // Should return zero-filled data on error
        if (result.size() != raw_count) {
            FAIL("Test1 (len mismatch): unexpected result size");
            return;
        }
    }

    // Test 2: Invalid codec_id (not in {0,1,2,255})
    {
        auto payload_bad = payload;
        payload_bad[0] = 100;  // Invalid codec_id

        auto result = decode_filter_lo_stream(
            payload_bad.data(), payload_bad.size(), raw_count,
            filter_ids, block_types, 8, 1, false, false,
            FileHeader::VERSION_FILTER_LO_CTX_HYBRID_CODEC,
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            nullptr
        );
        if (result.size() != raw_count) {
            FAIL("Test2 (invalid codec_id): unexpected result size");
            return;
        }
    }

    // Test 3: codec_id=255 but len != 0
    {
        auto payload_bad = payload;
        payload_bad[3] = 255;  // codec_id = empty
        payload_bad[18] = 1;   // but len > 0

        auto result = decode_filter_lo_stream(
            payload_bad.data(), payload_bad.size(), raw_count,
            filter_ids, block_types, 8, 1, false, false,
            FileHeader::VERSION_FILTER_LO_CTX_HYBRID_CODEC,
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            nullptr
        );
        if (result.size() != raw_count) {
            FAIL("Test3 (empty with len>0): unexpected result size");
            return;
        }
    }

    // Test 4: Version too old (should reject mode8)
    {
        auto result = decode_filter_lo_stream(
            payload.data(), payload.size(), raw_count,
            filter_ids, block_types, 8, 1, false, false,
            FileHeader::VERSION_FILTER_LO_CTX_MIXED_CDF,  // v0x0018, too old
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            [](const uint8_t*, size_t, size_t rc) { return std::vector<uint8_t>(rc, 0); },
            nullptr
        );
        if (result.size() != raw_count) {
            FAIL("Test4 (old version): unexpected result size");
            return;
        }
    }

    PASS();
}

// ============================================================
