#pragma once

#include "headers.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_mode_select.h"
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector>

namespace hakonyans::lossless_filter_rows {

// Zigzag encoding for signed int16 -> unsigned uint16
inline uint16_t zigzag_encode_val(int16_t val) {
    return (uint16_t)((val << 1) ^ (val >> 15));
}

enum class FilterRowCostModel : uint8_t { SAD = 0, BITS2 = 1, ENTROPY = 2 };

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
    if (std::strcmp(raw, "entropy") == 0 || std::strcmp(raw, "ENTROPY") == 0) {
        return FilterRowCostModel::ENTROPY;
    }
    return FilterRowCostModel::SAD;
}

inline int entropy_topk_env() {
    static const int kV = parse_env_int("HKN_FILTER_ROWS_ENTROPY_TOPK", 2, 1, 8);
    return kV;
}

inline int entropy_hi_weight_permille_env() {
    static const int kV = parse_env_int(
        "HKN_FILTER_ROWS_ENTROPY_HI_WEIGHT_PERMILLE", 350, 0, 2000
    );
    return kV;
}

inline double log2_count_cached(uint32_t v) {
    static const std::vector<double> kLog2Table = []() {
        constexpr uint32_t kMaxCached = 8192;
        std::vector<double> tbl(kMaxCached + 1, 0.0);
        for (uint32_t i = 1; i <= kMaxCached; i++) {
            tbl[i] = std::log2((double)i);
        }
        return tbl;
    }();
    if (v < kLog2Table.size()) return kLog2Table[v];
    return std::log2((double)v);
}

inline int64_t shannon_bits_fp64_from_hist256(
    const std::array<uint32_t, 256>& hist, uint32_t total_count
) {
    if (total_count == 0) return 0;
    const double log_total = log2_count_cached(total_count);
    double bits = 0.0;
    for (uint32_t c : hist) {
        if (c == 0) continue;
        bits += (double)c * (log_total - log2_count_cached(c));
    }
    return (int64_t)std::llround(bits * 64.0);
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

        auto predict = [](int f, int16_t a, int16_t b, int16_t c) -> int16_t {
            switch (f) {
                case 0: return 0;
                case 1: return a;
                case 2: return b;
                case 3: return (int16_t)(((int)a + (int)b) / 2);
                case 4: return LosslessFilter::paeth_predictor(a, b, c);
                case 5: return LosslessFilter::med_predictor(a, b, c);
                case 6: return (int16_t)(((int)a * 3 + (int)b) / 4);
                case 7: return (int16_t)(((int)a + (int)b * 3) / 4);
                default: return 0;
            }
        };

        int best_f = (force_filter_id >= 0) ? force_filter_id : 0;
        if (force_filter_id < 0) {
            std::array<int, 8> candidate_filters{};
            int candidate_count = 0;
            for (int f = 0; f < 8; f++) {
                if (f == 5 && profile_id != lossless_mode_select::PROFILE_PHOTO) continue;
                candidate_filters[candidate_count++] = f;
            }

            if (cost_model == FilterRowCostModel::ENTROPY) {
                // Stage 1: coarse ranking by fast BITS2 proxy
                std::array<int64_t, 8> coarse_scores{};
                coarse_scores.fill(std::numeric_limits<int64_t>::max());
                for (int ci = 0; ci < candidate_count; ci++) {
                    const int f = candidate_filters[ci];
                    int64_t sum = 0;
                    for (uint32_t x = 0; x < pad_w; x++) {
                        const int bx_col = (int)(x / 8);
                        if (block_types[(size_t)by_row * (size_t)nx + (size_t)bx_col] !=
                            FileHeader::BlockType::DCT) {
                            continue;
                        }

                        const int16_t orig = padded[(size_t)y * (size_t)pad_w + (size_t)x];
                        const int16_t a = (x > 0)
                            ? padded[(size_t)y * (size_t)pad_w + (size_t)(x - 1)]
                            : 0;
                        const int16_t b = (y > 0)
                            ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)x]
                            : 0;
                        const int16_t c = (x > 0 && y > 0)
                            ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)(x - 1)]
                            : 0;
                        const int diff = (int)orig - (int)predict(f, a, b, c);
                        sum += lossless_mode_select::estimate_filter_symbol_bits2_fast(
                            fast_abs(diff), bits_lut
                        );
                    }
                    coarse_scores[f] = sum;
                }

                const int topk = std::min(candidate_count, entropy_topk_env());
                std::array<int, 8> eval_filters{};
                int eval_count = 0;
                std::array<uint8_t, 8> selected{};
                for (int k = 0; k < topk; k++) {
                    int best_idx = -1;
                    for (int ci = 0; ci < candidate_count; ci++) {
                        const int f = candidate_filters[ci];
                        if (selected[f]) continue;
                        if (best_idx < 0 ||
                            coarse_scores[f] < coarse_scores[best_idx] ||
                            (coarse_scores[f] == coarse_scores[best_idx] && f < best_idx)) {
                            best_idx = f;
                        }
                    }
                    if (best_idx >= 0) {
                        selected[best_idx] = 1;
                        eval_filters[eval_count++] = best_idx;
                    }
                }

                const int hi_weight_permille = entropy_hi_weight_permille_env();
                int64_t best_cost_fp = std::numeric_limits<int64_t>::max();
                for (int ei = 0; ei < eval_count; ei++) {
                    const int f = eval_filters[ei];
                    std::array<uint32_t, 256> hist_lo{};
                    std::array<uint32_t, 256> hist_hi{};
                    uint32_t sample_count = 0;

                    for (uint32_t x = 0; x < pad_w; x++) {
                        const int bx_col = (int)(x / 8);
                        if (block_types[(size_t)by_row * (size_t)nx + (size_t)bx_col] !=
                            FileHeader::BlockType::DCT) {
                            continue;
                        }

                        const int16_t orig = padded[(size_t)y * (size_t)pad_w + (size_t)x];
                        const int16_t a = (x > 0)
                            ? padded[(size_t)y * (size_t)pad_w + (size_t)(x - 1)]
                            : 0;
                        const int16_t b = (y > 0)
                            ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)x]
                            : 0;
                        const int16_t c = (x > 0 && y > 0)
                            ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)(x - 1)]
                            : 0;
                        const int diff = (int)orig - (int)predict(f, a, b, c);
                        const uint16_t zz = zigzag_encode_val((int16_t)diff);
                        hist_lo[zz & 0xFF]++;
                        hist_hi[(zz >> 8) & 0xFF]++;
                        sample_count++;
                    }

                    if (sample_count == 0) continue;
                    const int64_t lo_bits_fp = shannon_bits_fp64_from_hist256(hist_lo, sample_count);
                    const int64_t hi_bits_fp = shannon_bits_fp64_from_hist256(hist_hi, sample_count);
                    const int64_t cost_fp =
                        lo_bits_fp + ((hi_bits_fp * (int64_t)hi_weight_permille + 500) / 1000);
                    if (cost_fp < best_cost_fp) {
                        best_cost_fp = cost_fp;
                        best_f = f;
                    }
                }
            } else {
                int64_t best_sum = std::numeric_limits<int64_t>::max();
                for (int ci = 0; ci < candidate_count; ci++) {
                    const int f = candidate_filters[ci];
                    int64_t sum = 0;
                    for (uint32_t x = 0; x < pad_w; x++) {
                        const int bx_col = (int)(x / 8);
                        if (block_types[(size_t)by_row * (size_t)nx + (size_t)bx_col] !=
                            FileHeader::BlockType::DCT) {
                            continue;
                        }

                        const int16_t orig = padded[(size_t)y * (size_t)pad_w + (size_t)x];
                        const int16_t a = (x > 0)
                            ? padded[(size_t)y * (size_t)pad_w + (size_t)(x - 1)]
                            : 0;
                        const int16_t b = (y > 0)
                            ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)x]
                            : 0;
                        const int16_t c = (x > 0 && y > 0)
                            ? padded[(size_t)(y - 1) * (size_t)pad_w + (size_t)(x - 1)]
                            : 0;
                        const int diff = (int)orig - (int)predict(f, a, b, c);
                        if (cost_model == FilterRowCostModel::BITS2) {
                            sum += lossless_mode_select::estimate_filter_symbol_bits2_fast(
                                fast_abs(diff), bits_lut
                            );
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

            const int16_t pred = predict(best_f, a, b, c);
            filter_residuals.push_back(orig - pred);
        }
    }
}

} // namespace hakonyans::lossless_filter_rows
