#pragma once

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace hakonyans {

/**
 * PNG-compatible prediction filters for lossless compression.
 *
 * Each row is filtered independently. Filter selection per-row
 * minimizes the sum of absolute residuals (same heuristic as PNG).
 *
 * Filter types:
 *   0 = None    : Filt(x) = Orig(x)
 *   1 = Sub     : Filt(x) = Orig(x) - Orig(a)             a = left
 *   2 = Up      : Filt(x) = Orig(x) - Orig(b)             b = above
 *   3 = Average : Filt(x) = Orig(x) - floor((a+b)/2)
 *   4 = Paeth   : Filt(x) = Orig(x) - PaethPredictor(a,b,c)
 *
 * Data layout (per component plane, int16_t values):
 *   Input:  width * height int16_t values (row-major)
 *   Output: height * (1 + width) bytes or int16_t, first byte = filter type
 */
class LosslessFilter {
public:
    enum FilterType : uint8_t {
        FILTER_NONE       = 0,
        FILTER_SUB        = 1,
        FILTER_UP         = 2,
        FILTER_AVERAGE    = 3,
        FILTER_PAETH      = 4,
        FILTER_MED        = 5,
        FILTER_WEIGHTED_A = 6, // 0.75*a + 0.25*b
        FILTER_WEIGHTED_B = 7, // 0.25*a + 0.75*b
        FILTER_COUNT      = 8
    };

    /**
     * Paeth predictor (identical to PNG spec)
     */
    static inline int16_t paeth_predictor(int16_t a, int16_t b, int16_t c) {
        int p  = (int)a + (int)b - (int)c;
        int pa = std::abs(p - (int)a);
        int pb = std::abs(p - (int)b);
        int pc = std::abs(p - (int)c);
        if (pa <= pb && pa <= pc) return a;
        if (pb <= pc) return b;
        return c;
    }

    /**
     * MED (Median Edge Detector) predictor (from JPEG-LS / LOCO-I)
     */
    static inline int16_t med_predictor(int16_t a, int16_t b, int16_t c) {
        if (c >= std::max(a, b)) return std::min(a, b);
        if (c <= std::min(a, b)) return std::max(a, b);
        return (int16_t)((uint16_t)a + (uint16_t)b - (uint16_t)c);
    }

    static inline int16_t predict(uint8_t ftype, int16_t a, int16_t b, int16_t c) {
        switch (ftype) {
            case FILTER_NONE:       return 0;
            case FILTER_SUB:        return a;
            case FILTER_UP:         return b;
            case FILTER_AVERAGE:    return (int16_t)(((int)a + (int)b) / 2);
            case FILTER_PAETH:      return paeth_predictor(a, b, c);
            case FILTER_MED:        return med_predictor(a, b, c);
            case FILTER_WEIGHTED_A: return (int16_t)(((int)a * 3 + (int)b) / 4);
            case FILTER_WEIGHTED_B: return (int16_t)(((int)a + (int)b * 3) / 4);
            default:                return 0;
        }
    }

    /**
     * Filter an image plane (int16_t values, e.g. YCoCg-R components).
     *
     * @param data      Input pixel data (width * height)
     * @param width     Image width
     * @param height    Image height
     * @param out_filter_ids  Output: per-row filter IDs (height entries)
     * @param out_filtered    Output: filtered residuals (width * height)
     */
    static void filter_image(
        const int16_t* data, int width, int height,
        std::vector<uint8_t>& out_filter_ids,
        std::vector<int16_t>& out_filtered
    ) {
        out_filter_ids.resize(height);
        out_filtered.resize(width * height);

        // Temporary buffers for each filter candidate
        std::vector<int16_t> candidates[FILTER_COUNT];
        for (int f = 0; f < FILTER_COUNT; f++) {
            candidates[f].resize(width);
        }

        for (int y = 0; y < height; y++) {
            const int16_t* row  = data + y * width;
            const int16_t* prev = (y > 0) ? data + (y - 1) * width : nullptr;

            // Compute all filter candidates for this row
            for (int x = 0; x < width; x++) {
                int16_t a = (x > 0) ? row[x - 1] : 0;       // left
                int16_t b = prev ? prev[x] : 0;              // above
                int16_t c = (x > 0 && prev) ? prev[x - 1] : 0; // upper-left

                candidates[FILTER_NONE][x]    = row[x];
                candidates[FILTER_SUB][x]     = row[x] - a;
                candidates[FILTER_UP][x]      = row[x] - b;
                candidates[FILTER_AVERAGE][x] = row[x] - (int16_t)((int)(a + b) / 2);
                candidates[FILTER_PAETH][x]   = row[x] - paeth_predictor(a, b, c);
                candidates[FILTER_MED][x]     = row[x] - med_predictor(a, b, c);
                candidates[FILTER_WEIGHTED_A][x] = row[x] - (int16_t)(((int)a * 3 + (int)b) / 4);
                candidates[FILTER_WEIGHTED_B][x] = row[x] - (int16_t)(((int)a + (int)b * 3) / 4);
            }

            // Select filter with minimal sum of absolute residuals
            int best_filter = 0;
            int64_t best_sum = INT64_MAX;
            for (int f = 0; f < FILTER_COUNT; f++) {
                int64_t sum = 0;
                for (int x = 0; x < width; x++) {
                    sum += std::abs((int)candidates[f][x]);
                }
                if (sum < best_sum) {
                    best_sum = sum;
                    best_filter = f;
                }
            }

            out_filter_ids[y] = (uint8_t)best_filter;
            std::memcpy(&out_filtered[y * width], candidates[best_filter].data(), width * sizeof(int16_t));
        }
    }

    /**
     * Unfilter (reconstruct) an image plane.
     *
     * @param filter_ids   Per-row filter IDs
     * @param filtered     Filtered residual data (width * height)
     * @param width        Image width
     * @param height       Image height
     * @param out_data     Output: reconstructed pixel data
     */
    static void unfilter_image(
        const uint8_t* filter_ids,
        const int16_t* filtered,
        int width, int height,
        std::vector<int16_t>& out_data
    ) {
        out_data.resize(width * height);

        for (int y = 0; y < height; y++) {
            const int16_t* frow = filtered + y * width;
            int16_t* orow = out_data.data() + y * width;
            const int16_t* prev = (y > 0) ? out_data.data() + (y - 1) * width : nullptr;
            uint8_t ftype = filter_ids[y];

            for (int x = 0; x < width; x++) {
                int16_t a = (x > 0) ? orow[x - 1] : 0;
                int16_t b = prev ? prev[x] : 0;
                int16_t c = (x > 0 && prev) ? prev[x - 1] : 0;

                switch (ftype) {
                case FILTER_NONE:
                    orow[x] = frow[x];
                    break;
                case FILTER_SUB:
                    orow[x] = frow[x] + a;
                    break;
                case FILTER_UP:
                    orow[x] = frow[x] + b;
                    break;
                case FILTER_AVERAGE:
                    orow[x] = frow[x] + (int16_t)((int)(a + b) / 2);
                    break;
                case FILTER_PAETH:
                    orow[x] = frow[x] + paeth_predictor(a, b, c);
                    break;
                case FILTER_MED:
                    orow[x] = frow[x] + med_predictor(a, b, c);
                    break;
                case FILTER_WEIGHTED_A:
                    orow[x] = frow[x] + (int16_t)(((int)a * 3 + (int)b) / 4);
                    break;
                case FILTER_WEIGHTED_B:
                    orow[x] = frow[x] + (int16_t)(((int)a + (int)b * 3) / 4);
                    break;
                default:
                    orow[x] = frow[x];
                    break;
                }
            }
        }
    }

    /**
     * Filter with a specific filter type (no auto-selection).
     * Useful for testing individual filters.
     */
    static void filter_row(
        const int16_t* row, const int16_t* prev,
        int width, FilterType ftype,
        int16_t* out
    ) {
        for (int x = 0; x < width; x++) {
            int16_t a = (x > 0) ? row[x - 1] : 0;
            int16_t b = prev ? prev[x] : 0;
            int16_t c = (x > 0 && prev) ? prev[x - 1] : 0;

            switch (ftype) {
            case FILTER_NONE:    out[x] = row[x]; break;
            case FILTER_SUB:     out[x] = row[x] - a; break;
            case FILTER_UP:      out[x] = row[x] - b; break;
            case FILTER_AVERAGE: out[x] = row[x] - (int16_t)((int)(a + b) / 2); break;
            case FILTER_PAETH:   out[x] = row[x] - paeth_predictor(a, b, c); break;
            case FILTER_MED:     out[x] = row[x] - med_predictor(a, b, c); break;
            case FILTER_WEIGHTED_A: out[x] = row[x] - (int16_t)(((int)a * 3 + (int)b) / 4); break;
            case FILTER_WEIGHTED_B: out[x] = row[x] - (int16_t)(((int)a + (int)b * 3) / 4); break;
            default:             out[x] = row[x]; break;
            }
        }
    }
};

} // namespace hakonyans
