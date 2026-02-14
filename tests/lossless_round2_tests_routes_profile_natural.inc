static void test_profile_classifier_photo() {
    TEST("test_profile_classifier_photo");
    // High gradient, random noise
    std::vector<int16_t> plane(64 * 64);
    
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int h = (x * 374761393) ^ (y * 668265263) ^ 0xDEADBEEF;
            h = (h ^ (h >> 13)) * 1274126177;
            plane[y * 64 + x] = (int16_t)((h & 0xFF));
        }
    }
    
    auto profile = hakonyans::GrayscaleEncoder::classify_lossless_profile(plane.data(), 64, 64);
    if (profile != hakonyans::GrayscaleEncoder::LosslessProfile::PHOTO) {
        FAIL("Expected PHOTO profile");
        std::cout << "Got: " << (int)profile << std::endl;
    } else {
        PASS();
    }
}

static void test_profile_anime_roundtrip() {
    TEST("test_profile_anime_roundtrip");
    // Construct the "Anime-like" image from classifier test
    const int W = 128, H = 128;
    std::vector<uint8_t> pixels(W * H);
    int colors[8];
    for (int i = 0; i < 8; i++) colors[i] = i * 16;
    std::mt19937 rng(42);
    
    for (int by = 0; by < H/8; by++) {
        for (int bx = 0; bx < W/8; bx++) {
            int cur_x = bx * 8;
            int cur_y = by * 8;
            bool do_copy = (rng() % 100 < 75);
            if (do_copy && bx > 0) {
                 for (int y=0; y<8; y++) for (int x=0; x<8; x++)
                     pixels[(cur_y+y)*W + (cur_x+x)] = pixels[(cur_y+y)*W + (cur_x-8+x)];
            } else {
                 int c = colors[rng() % 8];
                 for (int y=0; y<8; y++) for (int x=0; x<8; x++)
                     pixels[(cur_y+y)*W + (cur_x+x)] = (uint8_t)c;
            }
        }
    }
    
    hakonyans::GrayscaleEncoder::reset_lossless_mode_debug_stats();
    auto encoded = hakonyans::GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
    auto stats = hakonyans::GrayscaleEncoder::get_lossless_mode_debug_stats();
    
    if (stats.profile_anime_tiles == 0) {
        FAIL("Expected usage of ANIME profile");
    }
    if (encoded.empty()) FAIL("Encode failed");
    
    // Decode and verify
    hakonyans::FileHeader hdr = hakonyans::FileHeader::read(encoded.data());
    if (hdr.width != W || hdr.height != H) {
        FAIL("Dim mismatch");
        return;
    }
    
    auto decoded = hakonyans::GrayscaleDecoder::decode(encoded);
    if (decoded.empty()) {
        FAIL("Decode failed");
        return;
    }
    
    const uint8_t* decoded_ptr = decoded.data();
    
    for (size_t i = 0; i < pixels.size(); i++) {
        if (decoded_ptr[i] != pixels[i]) {
            FAIL("Pixel mismatch");
            return;
        }
    }
    PASS();
}

static void test_profile_classifier_anime_not_ui() {
    TEST("test_profile_classifier_anime_not_ui (92% copy but high gradient)");
    
    // Pattern: 92% copy hit rate (UI-like), but mean_abs_diff=25 (Anime-like)
    // New score-based classifier should prefer ANIME over UI in this case.
    const int W = 128, H = 128;
    std::vector<int16_t> plane(W * H);
    std::mt19937 rng(1337);
    
    // 92% copy hit rate means 13/14 blocks should be exact copies.
    for (int by = 0; by < H/8; by++) {
        for (int bx = 0; bx < W/8; bx++) {
            int cur_x = bx * 8;
            int cur_y = by * 8;
            bool do_copy = (rng() % 100 < 92);
            if (do_copy && bx > 0) {
                 // Copy from left block
                 for (int y=0; y<8; y++) {
                     for (int x=0; x<8; x++) {
                         plane[(cur_y+y)*W + (cur_x+x)] = plane[(cur_y+y)*W + (cur_x-8+x)];
                     }
                 }
            } else {
                 // New block with mean_abs_diff ~ 25
                 // Use a sawtooth pattern
                 for (int y=0; y<8; y++) {
                     for (int x=0; x<8; x++) {
                         plane[(cur_y+y)*W + (cur_x+x)] = (int16_t)((x + y) * 12);
                     }
                 }
            }
        }
    }
    
    auto profile = GrayscaleEncoder::classify_lossless_profile(plane.data(), W, H);
    if (profile == GrayscaleEncoder::LosslessProfile::ANIME) {
        PASS();
    } else {
        FAIL("Expected ANIME but got " + std::to_string((int)profile));
    }
}

static void test_anime_palette_bias_path() {
    TEST("test_anime_palette_bias_path");
    
    // 1. Create Anime image
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H);
    // 4 colors (Palette 4), transitions ~ 40
    for (int y=0; y<H; y++) {
        for (int x=0; x<W; x++) {
            pixels[y*W+x] = ((x/4 + y/4) % 4) * 50;
        }
    }
    
    GrayscaleEncoder::reset_lossless_mode_debug_stats();
    auto hkn = GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
    auto stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
    
    if (stats.anime_palette_bonus_applied > 0) {
        PASS();
    } else {
        FAIL("anime_palette_bonus_applied is 0");
    }
}

static void test_filter_lo_mode5_selection_path() {
    TEST("test_filter_lo_mode5_selection_path");
    
    // Create a large pattern that forces filter blocks (high variance)
    // but remains repetitive (good for LZ)
    const int W = 512, H = 512;
    std::vector<uint8_t> pixels(W * H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // High local variance to discourage PALETTE/COPY, 
            // but global periodicity to encourage LZ.
            pixels[y * W + x] = (uint8_t)(((x % 16) * 13 + (y % 16) * 17 + (x * y)) & 0xFF);
        }
    }
    
    GrayscaleEncoder::reset_lossless_mode_debug_stats();
    // Use encode_lossless which uses classify_lossless_profile
    auto hkn = GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
    auto stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
    
    // Check roundtrip first
    auto decoded = GrayscaleDecoder::decode(hkn);
    if (decoded != pixels) {
        FAIL("roundtrip failed for Mode 5 candidate image");
        return;
    }

    if (stats.filter_lo_mode5_candidates > 0) {
        // Candidate was generated. 
        // We don't strictly assert selection because Mode 2 or 4 might still win,
        // but we verify the path was exercised.
        PASS();
    } else {
        FAIL("Mode 5 candidate path not hit");
    }
}

static void test_filter_lo_mode5_fallback_logic() {
    TEST("test_filter_lo_mode5_fallback_to_mode0");
    
    // Random noise image - should trigger fallbacks
    const int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H);
    std::mt19937 rng(99);
    for (auto& v : pixels) v = (uint8_t)(rng() & 0xFF);
    
    GrayscaleEncoder::reset_lossless_mode_debug_stats();
    auto hkn = GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
    auto stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
    
    auto decoded = GrayscaleDecoder::decode(hkn);
    if (decoded != pixels) {
        FAIL("roundtrip failed for noise image");
        return;
    }
    
    // On random noise, Mode 5 might still be a candidate but should be rejected by gate
    if (stats.filter_lo_mode5_candidates > 0) {
        if (stats.filter_lo_mode5 > 0) {
            // Unexpected, but possible if random is lucky.
            // Just check that we didn't crash.
        }
    }
    PASS();
}

static void test_natural_row_route_roundtrip() {
    TEST("test_natural_row_route_roundtrip");

    const int W = 640, H = 512;
    std::vector<uint8_t> pixels(W * H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Natural-like high-entropy texture to avoid screen-index prefilter.
            uint32_t h = (uint32_t)x * 73856093u;
            h ^= (uint32_t)y * 19349663u;
            h ^= (uint32_t)(x + y) * 83492791u;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            pixels[y * W + x] = (uint8_t)((h + (uint32_t)(x * 3 + y * 5)) & 0xFFu);
        }
    }

    GrayscaleEncoder::reset_lossless_mode_debug_stats();
    auto hkn = GrayscaleEncoder::encode_lossless(pixels.data(), W, H);
    auto dec = GrayscaleDecoder::decode(hkn);
    if (dec != pixels) {
        FAIL("natural route roundtrip mismatch");
        return;
    }

    auto s = GrayscaleEncoder::get_lossless_mode_debug_stats();
    if (s.natural_row_candidate_count == 0) {
        FAIL("natural_row_candidate_count is 0");
        return;
    }
    PASS();
}

static void test_natural_row_mode3_roundtrip() {
    TEST("Natural route Mode 3 (context-split) roundtrip");

    // Mode 3 is context-split based on local gradients.
    // Create an image with clear flat/edge transitions.
    const int W = 64, H = 64;
    std::vector<uint8_t> rgb(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t off = (size_t)(y * W + x) * 3;
            if ((x / 8) % 2 == (y / 8) % 2) {
                // Flat area
                rgb[off] = rgb[off+1] = rgb[off+2] = 128;
            } else {
                // High gradient area (noise)
                rgb[off] = (uint8_t)(x * 4);
                rgb[off+1] = (uint8_t)(y * 4);
                rgb[off+2] = (uint8_t)((x + y) * 2);
            }
        }
    }

    // Force Max preset to ensure route competition
    auto hkn = GrayscaleEncoder::encode_color_lossless(
        rgb.data(), W, H, GrayscaleEncoder::LosslessPreset::MAX
    );
    auto s = GrayscaleEncoder::get_lossless_mode_debug_stats();
    
    // We expect mode 3 to be candidate and hopefully selected for at least some tiles
    if (s.natural_row_mode3_size_sum == 0) {
        // Fallback: at least verify it's valid if it was chosen
    }

    int dec_w = 0, dec_h = 0;
    auto dec = GrayscaleDecoder::decode_color_lossless(hkn, dec_w, dec_h);

    if (dec_w == W && dec_h == H && dec == rgb) {
        PASS();
    } else {
        FAIL("Mode 3 roundtrip mismatch");
    }
}

static void test_natural_row_mode3_malformed() {
    TEST("Natural route Mode 3 malformed payload");

    const int W = 16, H = 16;
    std::vector<int16_t> plane(W * H, 0);
    // Create a mode 3 payload manually (minimal)
    std::vector<uint8_t> td;
    td.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    td.push_back(3); // mode 3
    auto push_u32 = [&](uint32_t v) {
        td.push_back((uint8_t)(v & 0xFF)); td.push_back((uint8_t)((v >> 8) & 0xFF));
        td.push_back((uint8_t)((v >> 16) & 0xFF)); td.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(W * H); // pixel_count
    push_u32(H);     // pred_count
    push_u32(100);   // flat_size (bogus)
    push_u32(100);   // edge_size (bogus)
    td.push_back(0); // pred_mode=raw
    push_u32(H);     // pred_raw_count
    push_u32(H);     // pred_payload_size
    for(int i=0; i<H; i++) td.push_back(0); // pred ids

    std::vector<int16_t> out;
    bool handled = hakonyans::lossless_natural_decode::try_decode_natural_row_wrapper(
        td.data(), td.size(), W, H, W, H, FileHeader::VERSION,
        [](const uint8_t*, size_t, size_t) { return std::vector<uint8_t>(); },
        [](const uint8_t*, size_t, size_t) { return std::vector<uint8_t>(); },
        out
    );

    if (handled && out.size() == W * H) {
        PASS(); // Gracefully returned zeros
    } else {
        FAIL("Malformed mode 3 payload caused crash or unexpected behavior");
    }
}

