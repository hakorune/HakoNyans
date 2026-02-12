#pragma once

#include "copy.h"
#include "lossless_mode_debug_stats.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace hakonyans::lossless_profile_classifier {

enum class Profile : uint8_t { UI = 0, ANIME = 1, PHOTO = 2 };

inline Profile classify(
    const int16_t* y_plane,
    uint32_t width,
    uint32_t height,
    LosslessModeDebugStats* stats
) {
    if (!y_plane || width == 0 || height == 0) return Profile::PHOTO;

    const int bx = (int)((width + 7) / 8);
    const int by = (int)((height + 7) / 8);
    if (bx * by < 64) return Profile::PHOTO;

    const CopyParams kCopyCandidates[4] = {
        CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
    };

    auto sample_at = [&](int x, int y) -> int16_t {
        int sx = std::clamp(x, 0, (int)width - 1);
        int sy = std::clamp(y, 0, (int)height - 1);
        return y_plane[(size_t)sy * width + (size_t)sx];
    };

    int step = 4;
    int total_blocks = bx * by;
    if (total_blocks < 256) step = 1;
    else if (total_blocks < 1024) step = 2;

    int samples = 0;
    int copy_hits = 0;
    uint64_t sum_abs_diff = 0;
    uint64_t pixel_count = 0;
    uint32_t hist[16] = {0};

    for (int yb = 0; yb < by; yb += step) {
        for (int xb = 0; xb < bx; xb += step) {
            int cur_x = xb * 8;
            int cur_y = yb * 8;
            bool hit = false;

            for (const auto& cand : kCopyCandidates) {
                int src_x = cur_x + cand.dx;
                int src_y = cur_y + cand.dy;
                if (src_x < 0 || src_y < 0) continue;
                if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                bool match = true;
                for (int y = 0; y < 8 && match; y++) {
                    for (int x = 0; x < 8; x++) {
                        if (sample_at(cur_x + x, cur_y + y) != sample_at(src_x + x, src_y + y)) {
                            match = false;
                            break;
                        }
                    }
                }
                if (match) {
                    hit = true;
                    break;
                }
            }

            if (hit) copy_hits++;

            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int16_t val = sample_at(cur_x + x, cur_y + y);
                    int bin = std::clamp((int)val, 0, 255) / 16;
                    if (bin >= 0 && bin < 16) hist[bin]++;

                    if (x > 0) sum_abs_diff += (uint64_t)std::abs(val - sample_at(cur_x + x - 1, cur_y + y));
                    if (y > 0) sum_abs_diff += (uint64_t)std::abs(val - sample_at(cur_x + x, cur_y + y - 1));
                }
            }

            samples++;
            pixel_count += 64;
        }
    }

    if (samples < 32) return Profile::PHOTO;

    const double copy_hit_rate = (double)copy_hits / (double)samples;
    double mean_abs_diff = 0.0;
    if (pixel_count > 0) {
        mean_abs_diff = (double)sum_abs_diff / (double)pixel_count;
    }

    int active_bins = 0;
    for (int k = 0; k < 16; k++) {
        if (hist[k] > 0) active_bins++;
    }

    if (stats) {
        stats->class_eval_count++;
        stats->class_copy_hit_x1000_sum += (uint64_t)(copy_hit_rate * 1000.0);
        stats->class_mean_abs_diff_x1000_sum += (uint64_t)(mean_abs_diff * 1000.0);
        stats->class_active_bins_sum += (uint64_t)active_bins;
    }

    if (copy_hit_rate >= 0.10 && active_bins <= 6 && mean_abs_diff <= 1.2) {
        return Profile::ANIME;
    }

    int ui_score = 0;
    int anime_score = 0;

    if (copy_hit_rate >= 0.90) ui_score += 3;
    if (active_bins <= 10) ui_score += 2;
    if (mean_abs_diff <= 12) ui_score += 1;

    if (copy_hit_rate >= 0.60 && copy_hit_rate < 0.95) anime_score += 2;
    if (active_bins >= 8 && active_bins <= 24) anime_score += 2;
    if (mean_abs_diff <= 28) anime_score += 2;

    if (ui_score >= anime_score + 2) return Profile::UI;
    if (anime_score >= 3) return Profile::ANIME;
    return Profile::PHOTO;
}

} // namespace hakonyans::lossless_profile_classifier
