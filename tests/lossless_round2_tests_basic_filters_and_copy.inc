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
