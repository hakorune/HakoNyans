static void test_filter_rows_lzcost_gate() {
    TEST("test_filter_rows_lzcost_gate");

    // Photo-like deterministic input.
    const int W = 128, H = 128;
    std::vector<uint8_t> rgb((size_t)W * (size_t)H * 3);
    std::mt19937 rng(20260214);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& v : rgb) v = (uint8_t)dist(rng);

    // Force PHOTO profile so LZCOST path is always exercised.
    setenv("HKN_FORCE_LOSSLESS_PROFILE", "2", 1);
    setenv("HKN_FILTER_ROWS_COST_MODEL", "lzcost", 1);
    setenv("HKN_FILTER_ROWS_LZCOST_TOPK", "3", 1);
    setenv("HKN_FILTER_ROWS_LZCOST_MIN_ROW_LEN", "8", 1); // keep test robust on short rows

    struct GateRunResult {
        GrayscaleEncoder::LosslessModeDebugStats stats;
        bool roundtrip_ok = false;
    };

    auto run_with_margin = [&](int margin) -> GateRunResult {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", margin);
        setenv("HKN_FILTER_ROWS_LZCOST_MARGIN_PERMILLE", buf, 1);

        GrayscaleEncoder::reset_lossless_mode_debug_stats();
        auto hkn = GrayscaleEncoder::encode_color_lossless(
            rgb.data(), W, H, GrayscaleEncoder::LosslessPreset::MAX
        );
        auto stats = GrayscaleEncoder::get_lossless_mode_debug_stats();

        int dw = 0, dh = 0;
        auto dec = GrayscaleDecoder::decode_color(hkn, dw, dh);
        const bool roundtrip_ok =
            (dw == W) && (dh == H) && (dec == rgb);

        GateRunResult out;
        out.stats = stats;
        out.roundtrip_ok = roundtrip_ok;
        return out;
    };

    // Validate both permissive and aggressive margins.
    auto r_1000 = run_with_margin(1000);
    auto r_980 = run_with_margin(980);

    if (r_1000.stats.filter_rows_lzcost_rows_considered == 0 ||
        r_980.stats.filter_rows_lzcost_rows_considered == 0) {
        FAIL("lzcost considered 0 rows"); return;
    }
    if (!r_1000.roundtrip_ok) {
        FAIL("roundtrip failed at margin=1000"); return;
    }
    if (!r_980.roundtrip_ok) {
        FAIL("roundtrip failed at margin=980"); return;
    }

    // Cleanup
    unsetenv("HKN_FILTER_ROWS_COST_MODEL");
    unsetenv("HKN_FILTER_ROWS_LZCOST_TOPK");
    unsetenv("HKN_FILTER_ROWS_LZCOST_MARGIN_PERMILLE");
    unsetenv("HKN_FILTER_ROWS_LZCOST_MIN_ROW_LEN");
    unsetenv("HKN_FORCE_LOSSLESS_PROFILE");

    PASS();
}

static void test_lossless_preset_balanced_compat() {
    TEST("test_lossless_preset_balanced_compat");

    const int W = 48, H = 40;
    std::mt19937 rng(20260213);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> rgb(W * H * 3);
    for (auto& v : rgb) v = (uint8_t)dist(rng);

    auto hkn_default = GrayscaleEncoder::encode_color_lossless(rgb.data(), W, H);
    auto hkn_balanced = GrayscaleEncoder::encode_color_lossless(
        rgb.data(), W, H, GrayscaleEncoder::LosslessPreset::BALANCED
    );

    if (hkn_default == hkn_balanced) {
        PASS();
    } else {
        FAIL("default lossless preset output differs from balanced");
    }
}

static void test_lossless_preset_fast_max_roundtrip() {
    TEST("test_lossless_preset_fast_max_roundtrip");

    const int W = 96, H = 96;
    std::mt19937 rng(404);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> rgb(W * H * 3);
    for (auto& v : rgb) v = (uint8_t)dist(rng);

    auto hkn_fast = GrayscaleEncoder::encode_color_lossless(
        rgb.data(), W, H, GrayscaleEncoder::LosslessPreset::FAST
    );
    auto hkn_max = GrayscaleEncoder::encode_color_lossless(
        rgb.data(), W, H, GrayscaleEncoder::LosslessPreset::MAX
    );

    int fast_w = 0, fast_h = 0;
    auto dec_fast = GrayscaleDecoder::decode_color(hkn_fast, fast_w, fast_h);
    int max_w = 0, max_h = 0;
    auto dec_max = GrayscaleDecoder::decode_color(hkn_max, max_w, max_h);

    const bool fast_ok = (fast_w == W && fast_h == H && dec_fast == rgb);
    const bool max_ok = (max_w == W && max_h == H && dec_max == rgb);
    if (fast_ok && max_ok) {
        PASS();
    } else {
        FAIL("fast/max preset roundtrip mismatch");
    }
}

