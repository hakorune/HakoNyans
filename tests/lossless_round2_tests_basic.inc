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

static void build_filter_rows_all_dct(
    const std::vector<int16_t>& padded,
    uint32_t w,
    uint32_t h,
    int profile_id,
    std::vector<uint8_t>& filter_ids,
    std::vector<int16_t>& residuals
) {
    std::vector<FileHeader::BlockType> block_types((w / 8) * (h / 8), FileHeader::BlockType::DCT);
    LosslessModeDebugStats stats{};
    lossless_filter_rows::build_filter_rows_and_residuals(
        padded, w, h, (int)(w / 8), block_types, profile_id, &stats, filter_ids, residuals
    );
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

void test_filter_rows_force_paeth_env() {
    TEST("Filter rows force filter env (Paeth=4)");

    const int W = 32, H = 32;
    std::mt19937 rng(2468);
    std::uniform_int_distribution<int> dist(-128, 127);
    std::vector<int16_t> padded(W * H);
    for (auto& v : padded) v = (int16_t)dist(rng);

    std::vector<uint8_t> filter_ids;
    std::vector<int16_t> residuals;
    {
        ScopedEnvVar force_filter("HKN_FILTER_ROWS_FORCE_FILTER_ID", "4");
        ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "sad");
        build_filter_rows_all_dct(
            padded, W, H, lossless_mode_select::PROFILE_ANIME, filter_ids, residuals
        );
    }

    if (filter_ids.size() != (size_t)H) {
        FAIL("filter_ids size mismatch");
        return;
    }
    if (residuals.size() != (size_t)W * (size_t)H) {
        FAIL("residual size mismatch");
        return;
    }
    for (uint8_t fid : filter_ids) {
        if (fid != LosslessFilter::FILTER_PAETH) {
            FAIL("force filter id did not apply to all rows");
            return;
        }
    }
    PASS();
}

void test_filter_rows_bits2_differs_from_sad() {
    TEST("Filter rows bits2 cost differs from SAD");

    const int W = 32, H = 32;
    bool found_diff = false;

    ScopedEnvVar force_filter("HKN_FILTER_ROWS_FORCE_FILTER_ID", "-1");
    for (int seed = 1; seed <= 1024 && !found_diff; seed++) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(-128, 127);
        std::vector<int16_t> padded(W * H);
        for (auto& v : padded) v = (int16_t)dist(rng);

        std::vector<uint8_t> ids_sad, ids_bits2;
        std::vector<int16_t> resid_sad, resid_bits2;
        {
            ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "sad");
            build_filter_rows_all_dct(
                padded, W, H, lossless_mode_select::PROFILE_PHOTO, ids_sad, resid_sad
            );
        }
        {
            ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "bits2");
            build_filter_rows_all_dct(
                padded, W, H, lossless_mode_select::PROFILE_PHOTO, ids_bits2, resid_bits2
            );
        }
        if (ids_sad != ids_bits2) {
            found_diff = true;
        }
    }

    if (found_diff) {
        PASS();
    } else {
        FAIL("No seed produced a SAD/BITS2 filter selection difference");
    }
}

void test_filter_rows_bits2_env_roundtrip() {
    TEST("Filter rows bits2 env roundtrip");

    const int W = 64, H = 64;
    std::mt19937 rng(13579);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> pixels(W * H * 3);
    for (auto& v : pixels) v = (uint8_t)dist(rng);

    std::vector<uint8_t> hkn;
    {
        ScopedEnvVar force_filter("HKN_FILTER_ROWS_FORCE_FILTER_ID", "-1");
        ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "bits2");
        hkn = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    }
    int dw = 0, dh = 0;
    auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);
    if (dw != W || dh != H || decoded != pixels) {
        FAIL("bits2 env roundtrip mismatch");
        return;
    }
    PASS();
}

void test_filter_rows_entropy_differs_from_sad() {
    TEST("Filter rows entropy cost differs from SAD");

    const int W = 32, H = 32;
    bool found_diff = false;

    ScopedEnvVar force_filter("HKN_FILTER_ROWS_FORCE_FILTER_ID", "-1");
    for (int seed = 1; seed <= 2048 && !found_diff; seed++) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(-128, 127);
        std::vector<int16_t> padded(W * H);
        for (auto& v : padded) v = (int16_t)dist(rng);

        std::vector<uint8_t> ids_sad, ids_entropy;
        std::vector<int16_t> resid_sad, resid_entropy;
        {
            ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "sad");
            build_filter_rows_all_dct(
                padded, W, H, lossless_mode_select::PROFILE_PHOTO, ids_sad, resid_sad
            );
        }
        {
            ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "entropy");
            build_filter_rows_all_dct(
                padded, W, H, lossless_mode_select::PROFILE_PHOTO, ids_entropy, resid_entropy
            );
        }
        if (ids_sad != ids_entropy) {
            found_diff = true;
        }
    }

    if (found_diff) {
        PASS();
    } else {
        FAIL("No seed produced a SAD/ENTROPY filter selection difference");
    }
}

void test_filter_rows_entropy_env_roundtrip() {
    TEST("Filter rows entropy env roundtrip");

    const int W = 64, H = 64;
    std::mt19937 rng(24681357);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> pixels(W * H * 3);
    for (auto& v : pixels) v = (uint8_t)dist(rng);

    std::vector<uint8_t> hkn;
    {
        ScopedEnvVar force_filter("HKN_FILTER_ROWS_FORCE_FILTER_ID", "-1");
        ScopedEnvVar cost_model("HKN_FILTER_ROWS_COST_MODEL", "entropy");
        hkn = GrayscaleEncoder::encode_color_lossless(pixels.data(), W, H);
    }
    int dw = 0, dh = 0;
    auto decoded = GrayscaleDecoder::decode_color(hkn, dw, dh);
    if (dw != W || dh != H || decoded != pixels) {
        FAIL("entropy env roundtrip mismatch");
        return;
    }
    PASS();
}

// ============================================================
// Test 9: TILE_MATCH4 (4x4) Roundtrip
// ============================================================
void test_tile_match4_roundtrip() {
    TEST("TILE_MATCH4 (4x4) roundtrip");

    const int W = 32, H = 32;
    const int CW = W / 4, CH = H / 4; // 4x4 cell grid
    bool found_tile4_case = false;

    for (int seed = 1; seed <= 1024 && !found_tile4_case; seed++) {
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
