#pragma once

#include "headers.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_mode_select.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace hakonyans::lossless_filter_rows {

inline void build_filter_rows_and_residuals(
    const std::vector<int16_t>& padded,
    uint32_t pad_w,
    uint32_t pad_h,
    int nx,
    const std::vector<FileHeader::BlockType>& block_types,
    int profile_id,
    LosslessModeDebugStats* stats,
    std::vector<uint8_t>& filter_ids,
    std::vector<int16_t>& filter_residuals
) {
    filter_ids.assign(pad_h, 0);
    filter_residuals.clear();

    for (uint32_t y = 0; y < pad_h; y++) {
        const int by_row = (int)(y / 8);

        bool has_filter = false;
        for (int bx = 0; bx < nx; bx++) {
            if (block_types[(size_t)by_row * (size_t)nx + (size_t)bx] == FileHeader::BlockType::DCT) {
                has_filter = true;
                break;
            }
        }
        if (!has_filter) {
            filter_ids[y] = 0;
            continue;
        }

        int best_f = 0;
        int64_t best_sum = INT64_MAX;
        const int filter_count = lossless_mode_select::lossless_filter_candidates(profile_id);
        for (int f = 0; f < filter_count; f++) {
            int64_t sum = 0;
            for (uint32_t x = 0; x < pad_w; x++) {
                const int bx_col = (int)(x / 8);
                if (block_types[(size_t)by_row * (size_t)nx + (size_t)bx_col] != FileHeader::BlockType::DCT) continue;

                const int16_t orig = padded[(size_t)y * (size_t)pad_w + (size_t)x];
                const int16_t a = (x > 0) ? padded[(size_t)y * (size_t)pad_w + (size_t)(x - 1)] : 0;
                const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)x] : 0;
                const int16_t c = (x > 0 && y > 0)
                    ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)(x - 1)]
                    : 0;

                int16_t pred = 0;
                switch (f) {
                    case 0: pred = 0; break;
                    case 1: pred = a; break;
                    case 2: pred = b; break;
                    case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                    case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                    case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                    default: pred = 0; break;
                }
                sum += std::abs((int)(orig - pred));
            }
            if (sum < best_sum) {
                best_sum = sum;
                best_f = f;
            }
        }

        filter_ids[y] = (uint8_t)best_f;
        if (stats) {
            stats->filter_rows_with_pixels++;
            if (best_f >= 0 && best_f < 6) stats->filter_row_id_hist[best_f]++;
            if (best_f == 5) stats->filter_med_selected++;
        }

        for (uint32_t x = 0; x < pad_w; x++) {
            const int bx_col = (int)(x / 8);
            if (block_types[(size_t)by_row * (size_t)nx + (size_t)bx_col] != FileHeader::BlockType::DCT) continue;

            const int16_t orig = padded[(size_t)y * (size_t)pad_w + (size_t)x];
            const int16_t a = (x > 0) ? padded[(size_t)y * (size_t)pad_w + (size_t)(x - 1)] : 0;
            const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)x] : 0;
            const int16_t c = (x > 0 && y > 0)
                ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)(x - 1)]
                : 0;

            int16_t pred = 0;
            switch (best_f) {
                case 0: pred = 0; break;
                case 1: pred = a; break;
                case 2: pred = b; break;
                case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                default: pred = 0; break;
            }
            filter_residuals.push_back(orig - pred);
        }
    }
}

} // namespace hakonyans::lossless_filter_rows
