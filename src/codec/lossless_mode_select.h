#pragma once

#include "copy.h"
#include "palette.h"
#include "lossless_filter.h"

#include <algorithm>
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

inline int lossless_filter_candidates(int profile) {
    return (profile == PROFILE_PHOTO) ? LosslessFilter::FILTER_COUNT : LosslessFilter::FILTER_MED;
}

inline int estimate_filter_bits(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, int cur_x, int cur_y, int profile
) {
    (void)pad_h;
    int best_bits2 = std::numeric_limits<int>::max();
    const int filter_count = lossless_filter_candidates(profile);
    for (int f = 0; f < filter_count; f++) {
        int bits2 = 4; // block_type (2 bits * 2)
        bits2 += 6;    // effective filter_id overhead (3 bits * 2)
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int px = cur_x + x;
                int py = cur_y + y;
                int16_t orig = padded[py * (int)pad_w + px];
                int16_t a = (px > 0) ? padded[py * (int)pad_w + (px - 1)] : 0;
                int16_t b = (py > 0) ? padded[(py - 1) * (int)pad_w + px] : 0;
                int16_t c = (px > 0 && py > 0) ? padded[(py - 1) * (int)pad_w + (px - 1)] : 0;
                int16_t pred = 0;
                switch (f) {
                    case 0: pred = 0; break;
                    case 1: pred = a; break;
                    case 2: pred = b; break;
                    case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                    case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                    case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                }
                int abs_r = std::abs((int)orig - (int)pred);
                bits2 += estimate_filter_symbol_bits2(abs_r, profile);
            }
        }
        best_bits2 = std::min(best_bits2, bits2);
    }
    return best_bits2;
}

} // namespace hakonyans::lossless_mode_select

