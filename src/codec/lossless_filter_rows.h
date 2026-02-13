#pragma once

#include "headers.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_mode_select.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_filter_rows {

enum class FilterRowCostModel : uint8_t { SAD = 0, BITS2 = 1 };

inline int parse_env_int(const char* key, int fallback, int min_v, int max_v) {
    const char* raw = std::getenv(key);
    if (!raw || raw[0] == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0') return fallback;
    if (v < (long)min_v || v > (long)max_v) return fallback;
    return (int)v;
}

inline FilterRowCostModel parse_cost_model_env() {
    const char* raw = std::getenv("HKN_FILTER_ROWS_COST_MODEL");
    if (!raw || raw[0] == '\0') return FilterRowCostModel::SAD;
    if (std::strcmp(raw, "bits2") == 0 || std::strcmp(raw, "BITS2") == 0) {
        return FilterRowCostModel::BITS2;
    }
    return FilterRowCostModel::SAD;
}

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
    const int force_filter_id = parse_env_int("HKN_FILTER_ROWS_FORCE_FILTER_ID", -1, -1, 7);
    const FilterRowCostModel cost_model = parse_cost_model_env();
    const auto& bits_lut = lossless_mode_select::filter_symbol_bits2_lut(profile_id);
    auto fast_abs = [](int v) -> int { return (v < 0) ? -v : v; };

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

        int best_f = (force_filter_id >= 0) ? force_filter_id : 0;
        if (force_filter_id < 0) {
            int64_t best_sum = INT64_MAX;
            for (int f = 0; f < 8; f++) {
                if (f == 5 && profile_id != lossless_mode_select::PROFILE_PHOTO) continue;
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
                        case 6: pred = (int16_t)(((int)a * 3 + (int)b) / 4); break;
                        case 7: pred = (int16_t)(((int)a + (int)b * 3) / 4); break;
                        default: pred = 0; break;
                    }
                    const int diff = (int)orig - (int)pred;
                    if (cost_model == FilterRowCostModel::BITS2) {
                        sum += lossless_mode_select::estimate_filter_symbol_bits2_fast(fast_abs(diff), bits_lut);
                    } else {
                        sum += fast_abs(diff);
                    }
                }
                if (sum < best_sum) {
                    best_sum = sum;
                    best_f = f;
                }
            }
        }

        filter_ids[y] = (uint8_t)best_f;
        if (stats) {
            stats->filter_rows_with_pixels++;
            if (best_f >= 0 && best_f < 8) stats->filter_row_id_hist[best_f]++;
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
                case 6: pred = (int16_t)(((int)a * 3 + (int)b) / 4); break;
                case 7: pred = (int16_t)(((int)a + (int)b * 3) / 4); break;
                default: pred = 0; break;
            }
            filter_residuals.push_back(orig - pred);
        }
    }
}

} // namespace hakonyans::lossless_filter_rows
