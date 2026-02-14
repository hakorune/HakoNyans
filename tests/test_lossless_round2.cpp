#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <random>
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include "../src/codec/headers.h"
#include "../src/codec/lossless_natural_decode.h"
#include "../src/codec/lossless_filter.h"
#include "../src/codec/lossless_filter_rows.h"
#include "../src/codec/lossless_filter_lo_decode.h"
#include "../src/codec/lz_tile.h"

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

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* key, const char* value) : key_(key), had_old_(false) {
        const char* old = std::getenv(key_.c_str());
        if (old) {
            had_old_ = true;
            old_ = old;
        }
        if (value) {
            setenv(key_.c_str(), value, 1);
        } else {
            unsetenv(key_.c_str());
        }
    }

    ~ScopedEnvVar() {
        if (had_old_) {
            setenv(key_.c_str(), old_.c_str(), 1);
        } else {
            unsetenv(key_.c_str());
        }
    }

private:
    std::string key_;
    std::string old_;
    bool had_old_;
};

// ============================================================
// Test 1: Grayscale Lossless Roundtrip (bit-exact)
// ============================================================
#include "lossless_round2_tests_basic.inc"
#include "lossless_round2_tests_routes.inc"
#include "lossless_round2_tests_mode6.inc"
#include "lossless_round2_tests_extra.inc"

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
    test_filter_rows_force_paeth_env();
    test_filter_rows_bits2_differs_from_sad();
    test_filter_rows_bits2_env_roundtrip();
    test_filter_rows_entropy_differs_from_sad();
    test_filter_rows_entropy_env_roundtrip();
    test_tile_match4_roundtrip();
    test_copy_mode3_long_runs();
    test_copy_mode3_mixed_runs();
    test_copy_mode3_malformed();
    test_filter_ids_rans_roundtrip();
    test_filter_ids_lz_roundtrip();
    test_filter_hi_sparse_roundtrip();
    test_filter_wrapper_malformed();
    test_filter_lo_delta_roundtrip();
    test_filter_lo_lz_roundtrip();
    test_filter_lo_lz_rans_pipeline();
    test_tile_lz_core_roundtrip();
    test_filter_lo_malformed();
    test_filter_lo_mode3_roundtrip();
    test_filter_lo_mixed_rows();
    test_filter_lo_mode3_malformed();
    test_filter_lo_mode4_roundtrip();
    test_filter_lo_mode4_sparse_contexts();
    test_filter_lo_mode4_malformed();
    test_screen_indexed_tile_roundtrip();
    test_screen_indexed_anime_guard();
    test_screen_indexed_ui_adopt();
    test_palette_reorder_roundtrip();
    test_palette_reorder_two_color_canonical();
    test_profile_classifier_ui();
    test_profile_classifier_anime();
    test_profile_classifier_photo();
    test_profile_classifier_anime_not_ui();
    test_profile_anime_roundtrip();
    test_anime_palette_bias_path();
    test_filter_lo_mode5_selection_path();
    test_filter_lo_mode5_fallback_logic();
    test_natural_row_route_roundtrip();
    test_natural_row_mode3_roundtrip();
    test_natural_row_mode3_malformed();
    test_filter_rows_lzcost_gate();
    test_lossless_preset_balanced_compat();
    test_lossless_preset_fast_max_roundtrip();
    test_filter_lo_mode6_v15_backward_compat();
    test_filter_lo_mode6_v16_compact_dist();
    test_filter_lo_mode6_v17_typebit_lensplit();

    // Mode 8 tests (Phase 9X-5)
    test_filter_lo_mode8_roundtrip();
    test_filter_lo_mode8_malformed();

    // LZCOST filter row selection tests (Phase 9X-3)
    test_filter_rows_lzcost_roundtrip();
    test_filter_rows_lzcost_deterministic();
    test_filter_rows_lzcost_photo_only_disabled();
    test_filter_rows_lzcost_env_default_compat();
    test_csv_column_count_consistency();

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run << " passed ===" << std::endl;
    return (tests_passed == tests_run) ? 0 : 1;
}
