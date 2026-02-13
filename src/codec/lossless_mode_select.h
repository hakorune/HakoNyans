#pragma once

#include "copy.h"
#include "palette.h"
#include "lossless_filter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace hakonyans::lossless_mode_select {

inline constexpr int PROFILE_UI = 0;
inline constexpr int PROFILE_ANIME = 1;
inline constexpr int PROFILE_PHOTO = 2;

// ------------------------------------------------------------------------
// Lossless mode bit estimators (coarse heuristics for mode decision only)
// Units: 1 unit = 0.5 bits (scaled by 2)
// ------------------------------------------------------------------------
inline int estimate_copy_bits(const CopyParams& cp, int tile_width, int profile) {
    (void)tile_width;
    int bits2 = 4;  // block_type (2 bits * 2)
    int small_idx = CopyCodec::small_vector_index(cp);
    if (small_idx >= 0) {
        bits2 += 4;  // small-vector code (2 bits * 2)
        bits2 += 4;  // amortized stream/mode overhead (2 bits * 2)
    } else {
        bits2 += 64; // raw dx/dy payload fallback (32 bits * 2)
    }

    if (profile == PROFILE_PHOTO) bits2 += 8;      // +4 bits
    else if (profile == PROFILE_ANIME) bits2 += 6; // +3 bits
    return bits2;
}

inline int estimate_palette_index_bits_per_pixel(int palette_size) {
    if (palette_size <= 1) return 0;
    if (palette_size <= 2) return 1;
    if (palette_size <= 4) return 2;
    return 3;
}

inline int estimate_palette_bits(const Palette& p, int transitions, int profile) {
    if (p.size == 0) return std::numeric_limits<int>::max();
    int bits2 = 4;   // block_type (2 bits * 2)
    bits2 += 16;     // per-block palette header (8 bits * 2)
    int wide_colors = 0;
    for (int i = 0; i < p.size; i++) {
        if (p.colors[i] < -128 || p.colors[i] > 127) wide_colors++;
    }
    bits2 += ((int)p.size - wide_colors) * 16; // 8 bits * 2
    bits2 += wide_colors * 32;                 // 16 bits * 2

    if (p.size <= 1) return bits2;
    if (p.size == 2) {
        bits2 += (transitions <= 24) ? 48 : 128; // (24/64 bits * 2)
        if (profile != PROFILE_PHOTO && transitions <= 16) bits2 -= 16;
        return bits2;
    }

    int bits_per_index = estimate_palette_index_bits_per_pixel((int)p.size);
    bits2 += 64 * bits_per_index * 2;
    if (profile != PROFILE_PHOTO) {
        if (transitions <= 16) bits2 -= 96;
        else if (transitions <= 24) bits2 -= 64;
        else if (transitions <= 32) bits2 -= 32;
    } else {
        bits2 += ((int)p.size - wide_colors) * 16;
        bits2 += wide_colors * 32;
    }
    return bits2;
}

inline int estimate_filter_symbol_bits2(int abs_residual, int profile) {
    if (abs_residual == 0) return (profile == PROFILE_PHOTO) ? 1 : 2;
    if (abs_residual <= 1) return 4;
    if (abs_residual <= 3) return 6;
    if (abs_residual <= 7) return 8;
    if (abs_residual <= 15) return 10;
    if (abs_residual <= 31) return 12;
    if (abs_residual <= 63) return 14;
    if (abs_residual <= 127) return 16;
    return 20;
}

inline const std::array<uint8_t, 256>& filter_symbol_bits2_lut(int profile) {
    static const std::array<uint8_t, 256> kLutDefault = []() {
        std::array<uint8_t, 256> lut{};
        for (int i = 0; i < 256; i++) {
            lut[(size_t)i] = (uint8_t)estimate_filter_symbol_bits2(i, PROFILE_UI);
        }
        return lut;
    }();
    static const std::array<uint8_t, 256> kLutPhoto = []() {
        std::array<uint8_t, 256> lut{};
        for (int i = 0; i < 256; i++) {
            lut[(size_t)i] = (uint8_t)estimate_filter_symbol_bits2(i, PROFILE_PHOTO);
        }
        return lut;
    }();
    return (profile == PROFILE_PHOTO) ? kLutPhoto : kLutDefault;
}

inline int estimate_filter_symbol_bits2_fast(
    int abs_residual,
    const std::array<uint8_t, 256>& lut
) {
    if (abs_residual < 256) return lut[(size_t)abs_residual];
    return 20;
}

inline int lossless_filter_candidates(int profile) {
    (void)profile;
    return LosslessFilter::FILTER_COUNT;
}

inline int estimate_filter_bits(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, int cur_x, int cur_y, int profile
) {
    (void)pad_h;
    const int filter_count = lossless_filter_candidates(profile);
    const bool use_med = (filter_count == LosslessFilter::FILTER_COUNT);
    const auto& bits_lut = filter_symbol_bits2_lut(profile);
    auto fast_abs = [](int v) -> int { return (v < 0) ? -v : v; };
    int bits2[LosslessFilter::FILTER_COUNT];
    for (int f = 0; f < LosslessFilter::FILTER_COUNT; f++) bits2[f] = 10;

    for (int y = 0; y < 8; y++) {
        const int py = cur_y + y;
        const int16_t* row = padded + (size_t)py * (size_t)pad_w + (size_t)cur_x;
        const int16_t* up_row = (py > 0) ? (padded + (size_t)(py - 1) * (size_t)pad_w + (size_t)cur_x) : nullptr;

        int16_t a = (cur_x > 0) ? row[-1] : 0;
        int16_t c = (up_row && cur_x > 0) ? up_row[-1] : 0;

        for (int x = 0; x < 8; x++) {
            const int16_t orig = row[x];
            const int16_t b = up_row ? up_row[x] : 0;

            const int r0 = (int)orig;
            const int r1 = (int)orig - (int)a;
            const int r2 = (int)orig - (int)b;
            const int r3 = (int)orig - (((int)a + (int)b) / 2);
            const int r4 = (int)orig - (int)LosslessFilter::paeth_predictor(a, b, c);
            
            bits2[0] += estimate_filter_symbol_bits2_fast(fast_abs(r0), bits_lut);
            bits2[1] += estimate_filter_symbol_bits2_fast(fast_abs(r1), bits_lut);
            bits2[2] += estimate_filter_symbol_bits2_fast(fast_abs(r2), bits_lut);
            bits2[3] += estimate_filter_symbol_bits2_fast(fast_abs(r3), bits_lut);
            bits2[4] += estimate_filter_symbol_bits2_fast(fast_abs(r4), bits_lut);
            
            if (profile == PROFILE_PHOTO) {
                const int r5 = (int)orig - (int)LosslessFilter::med_predictor(a, b, c);
                bits2[5] += estimate_filter_symbol_bits2_fast(fast_abs(r5), bits_lut);
            }
            
            const int r6 = (int)orig - (int16_t)(((int)a * 3 + (int)b) / 4);
            const int r7 = (int)orig - (int16_t)(((int)a + (int)b * 3) / 4);
            bits2[6] += estimate_filter_symbol_bits2_fast(fast_abs(r6), bits_lut);
            bits2[7] += estimate_filter_symbol_bits2_fast(fast_abs(r7), bits_lut);

            a = orig;
            c = b;
        }
    }

    int best_bits2 = bits2[0];
    for (int f = 1; f < 8; f++) {
        if (f == 5 && profile != PROFILE_PHOTO) continue;
        best_bits2 = std::min(best_bits2, bits2[f]);
    }
    return best_bits2;
}

} // namespace hakonyans::lossless_mode_select
