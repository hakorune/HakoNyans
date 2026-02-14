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

enum class FilterRowCostModel : uint8_t { SAD = 0, BITS2 = 1, ENTROPY = 2, LZCOST = 3 };

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

inline bool try_parse_cost_model_token(const char* raw, FilterRowCostModel& out) {
    if (!raw || raw[0] == '\0') return false;
    if (std::strcmp(raw, "sad") == 0 || std::strcmp(raw, "SAD") == 0) {
        out = FilterRowCostModel::SAD;
        return true;
    }
    if (std::strcmp(raw, "bits2") == 0 || std::strcmp(raw, "BITS2") == 0) {
        out = FilterRowCostModel::BITS2;
        return true;
    }
    if (std::strcmp(raw, "entropy") == 0 || std::strcmp(raw, "ENTROPY") == 0) {
        out = FilterRowCostModel::ENTROPY;
        return true;
    }
    if (std::strcmp(raw, "lzcost") == 0 || std::strcmp(raw, "LZCOST") == 0) {
        out = FilterRowCostModel::LZCOST;
        return true;
    }
    return false;
}

inline bool try_parse_cost_model_env(FilterRowCostModel& out) {
    const char* raw = std::getenv("HKN_FILTER_ROWS_COST_MODEL");
    return try_parse_cost_model_token(raw, out);
}

inline FilterRowCostModel resolve_cost_model(
    FilterRowCostModel preset_default
) {
    FilterRowCostModel env_model = preset_default;
    if (try_parse_cost_model_env(env_model)) {
        return env_model;
    }
    return preset_default;
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

// LZCOST configuration environment variables
inline int lzcost_topk_env() {
    static const int kV = parse_env_int("HKN_FILTER_ROWS_LZCOST_TOPK", 3, 1, 4);
    return kV;
}

inline int lzcost_window_env() {
    static const int kV = parse_env_int("HKN_FILTER_ROWS_LZCOST_WINDOW", 256, 64, 1024);
    return kV;
}

inline bool lzcost_photo_only_env() {
    static const int kV = parse_env_int("HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY", 1, 0, 1);
    return kV != 0;
}

inline int lzcost_margin_permille_env() {
    static const int kV = parse_env_int("HKN_FILTER_ROWS_LZCOST_MARGIN_PERMILLE", 995, 900, 1000);
    return kV;
}

inline int lzcost_min_row_len_env() {
    static const int kV = parse_env_int("HKN_FILTER_ROWS_LZCOST_MIN_ROW_LEN", 64, 8, 2048);
    return kV;
}

// LZ cost estimation for a row of residuals
inline uint32_t lzcost_estimate_row(
    const uint8_t* row_residuals,
    uint32_t width,
    uint32_t window_size
) {
    if (width == 0) return 0;
    const uint32_t eval_len = std::min(width, window_size);

    uint32_t cost = 0;
    uint32_t pos = 0;

    while (pos < eval_len) {
        uint32_t best_len = 0;
        const uint32_t search_start = (pos > window_size) ? (pos - window_size) : 0;
        for (uint32_t back = search_start; back < pos; back++) {
            uint32_t len = 0;
            while (pos + len < eval_len &&
                   row_residuals[back + len] == row_residuals[pos + len] &&
                   len < 255) {
                len++;
            }
            if (len >= 3 && len > best_len) {
                best_len = len;
            }
        }

        if (best_len >= 3) {
            cost += 4;
            pos += best_len;
        } else {
            cost += 1;
            pos++;
        }
    }

    return cost;
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
    std::vector<int16_t>& filter_residuals,
    FilterRowCostModel preset_cost_model = FilterRowCostModel::SAD
) {
    filter_ids.assign(pad_h, 0);
    filter_residuals.clear();
    const int force_filter_id = parse_env_int("HKN_FILTER_ROWS_FORCE_FILTER_ID", -1, -1, 7);
    const FilterRowCostModel cost_model = resolve_cost_model(preset_cost_model);
    const auto& bits_lut = lossless_mode_select::filter_symbol_bits2_lut(profile_id);
    auto fast_abs = [](int v) -> int { return (v < 0) ? -v : v; };

    for (uint32_t y = 0; y < pad_h; y++) {
        const int by_row = (int)(y / 8);
        bool has_filter = false;
        for (int bx = 0; bx < nx; bx++) {
            if (block_types[(size_t)by_row * nx + bx] == FileHeader::BlockType::DCT) {
                has_filter = true;
                break;
            }
        }
        if (!has_filter) { filter_ids[y] = 0; continue; }

        int best_f = (force_filter_id >= 0) ? force_filter_id : 0;
        if (force_filter_id < 0) {
            std::array<int, 8> candidate_filters{};
            int candidate_count = 0;
            for (int f = 0; f < 8; f++) {
                if (f == 5 && profile_id != lossless_mode_select::PROFILE_PHOTO) continue;
                candidate_filters[candidate_count++] = f;
            }

            if (cost_model == FilterRowCostModel::ENTROPY || cost_model == FilterRowCostModel::LZCOST) {
                std::array<int64_t, 8> coarse_scores{};
                coarse_scores.fill(std::numeric_limits<int64_t>::max());
                for (int ci = 0; ci < candidate_count; ci++) {
                    const int f = candidate_filters[ci];
                    int64_t sum = 0;
                    for (uint32_t x = 0; x < pad_w; x++) {
                        if (block_types[(size_t)by_row * nx + (x / 8)] != FileHeader::BlockType::DCT) continue;
                        const int16_t a = (x > 0) ? padded[(size_t)y * pad_w + (x - 1)] : 0;
                        const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * pad_w + x] : 0;
                        const int16_t c = (x > 0 && y > 0) ? padded[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
                        int16_t diff = (int16_t)((uint16_t)padded[(size_t)y * pad_w + x] - (uint16_t)LosslessFilter::predict((uint8_t)f, a, b, c));
                        sum += lossless_mode_select::estimate_filter_symbol_bits2_fast(fast_abs(diff), bits_lut);
                    }
                    coarse_scores[f] = sum;
                }

                bool lz_done = false;
                if (cost_model == FilterRowCostModel::LZCOST) {
                    uint32_t active_row_len = 0;
                    for (uint32_t x = 0; x < pad_w; x++) if (block_types[(size_t)by_row * nx + (x / 8)] == FileHeader::BlockType::DCT) active_row_len++;
                    if (active_row_len >= (uint32_t)lzcost_min_row_len_env() &&
                        (!lzcost_photo_only_env() || profile_id == lossless_mode_select::PROFILE_PHOTO)) {
                        if (stats) { stats->filter_rows_lzcost_eval_rows++; stats->filter_rows_lzcost_rows_considered++; }
                        const int topk = std::min(candidate_count, lzcost_topk_env());
                        if (stats) stats->filter_rows_lzcost_topk_sum += topk;
                        std::array<int, 8> ev_fs{}; int ev_cnt = 0; std::array<uint8_t, 8> sel{}; sel.fill(0);
                        for (int k = 0; k < topk; k++) {
                            int b_idx = -1;
                            for (int ci = 0; ci < candidate_count; ci++) {
                                int f = candidate_filters[ci];
                                if (!sel[f] && (b_idx < 0 || coarse_scores[f] < coarse_scores[b_idx] || (coarse_scores[f] == coarse_scores[b_idx] && f < b_idx))) b_idx = f;
                            }
                            if (b_idx >= 0) { sel[b_idx] = 1; ev_fs[ev_cnt++] = b_idx; }
                        }
                        int base_f = ev_fs[0];
                        for (int ci = 0; ci < candidate_count; ci++) {
                            int f = candidate_filters[ci];
                            if (coarse_scores[f] < coarse_scores[base_f] || (coarse_scores[f] == coarse_scores[base_f] && f < base_f)) base_f = f;
                        }
                        if (!sel[base_f]) { ev_fs[ev_cnt++] = base_f; sel[base_f] = 1; }
                        uint32_t best_lz = std::numeric_limits<uint32_t>::max(); int b_lz_f = ev_fs[0]; uint32_t base_lz = 0;
                        for (int ei = 0; ei < ev_cnt; ei++) {
                            int f = ev_fs[ei]; std::vector<uint8_t> res; res.reserve(active_row_len);
                            for (uint32_t x = 0; x < pad_w; x++) {
                                if (block_types[(size_t)by_row * nx + (x / 8)] != FileHeader::BlockType::DCT) continue;
                                const int16_t a = (x > 0) ? padded[(size_t)y * pad_w + (x - 1)] : 0;
                                const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * pad_w + x] : 0;
                                const int16_t c = (x > 0 && y > 0) ? padded[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
                                int16_t diff = (int16_t)((uint16_t)padded[(size_t)y * pad_w + x] - (uint16_t)LosslessFilter::predict((uint8_t)f, a, b, c));
                                int adiff = fast_abs(diff);
                                res.push_back((adiff < 256) ? (uint8_t)adiff : (uint8_t)(adiff % 251 + 1));
                            }
                            uint32_t c_lz = lzcost_estimate_row(res.data(), (uint32_t)res.size(), lzcost_window_env());
                            if (f == base_f) base_lz = c_lz;
                            if (c_lz < best_lz || (c_lz == best_lz && f < b_lz_f)) { best_lz = c_lz; b_lz_f = f; }
                        }
                        if ((uint64_t)best_lz * 1000ull <= (uint64_t)base_lz * (uint64_t)lzcost_margin_permille_env()) {
                            best_f = b_lz_f; lz_done = true;
                            if (stats) { stats->filter_rows_lzcost_rows_adopted++; stats->filter_rows_lzcost_base_cost_sum += base_lz; stats->filter_rows_lzcost_best_cost_sum += best_lz; }
                        } else if (stats) stats->filter_rows_lzcost_rows_rejected_margin++;
                    }
                }

                if (!lz_done) {
                    if (cost_model == FilterRowCostModel::ENTROPY || preset_cost_model == FilterRowCostModel::ENTROPY) {
                        const int topk = std::min(candidate_count, entropy_topk_env());
                        std::array<int, 8> ev_fs{}; int ev_cnt = 0; std::array<uint8_t, 8> sel{}; sel.fill(0);
                        for (int k = 0; k < topk; k++) {
                            int b_idx = -1;
                            for (int ci = 0; ci < candidate_count; ci++) {
                                int f = candidate_filters[ci];
                                if (!sel[f] && (b_idx < 0 || coarse_scores[f] < coarse_scores[b_idx] || (coarse_scores[f] == coarse_scores[b_idx] && f < b_idx))) b_idx = f;
                            }
                            if (b_idx >= 0) { sel[b_idx] = 1; ev_fs[ev_cnt++] = b_idx; }
                        }
                        int64_t best_c = std::numeric_limits<int64_t>::max();
                        int f_ent = ev_fs[0];
                        for (int ei = 0; ei < ev_cnt; ei++) {
                            int f = ev_fs[ei]; std::array<uint32_t, 256> h_lo{}, h_hi{}; uint32_t sc = 0;
                            for (uint32_t x = 0; x < pad_w; x++) {
                                if (block_types[(size_t)by_row * nx + (x / 8)] != FileHeader::BlockType::DCT) continue;
                                const int16_t a = (x > 0) ? padded[(size_t)y * pad_w + (x - 1)] : 0;
                                const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * pad_w + x] : 0;
                                const int16_t c = (x > 0 && y > 0) ? padded[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
                                int16_t diff = (int16_t)((uint16_t)padded[(size_t)y * pad_w + x] - (uint16_t)LosslessFilter::predict((uint8_t)f, a, b, c));
                                uint16_t zz = zigzag_encode_val(diff);
                                h_lo[zz & 0xFF]++; h_hi[(zz >> 8) & 0xFF]++; sc++;
                            }
                            if (sc == 0) continue;
                            int64_t c = shannon_bits_fp64_from_hist256(h_lo, sc) + ((shannon_bits_fp64_from_hist256(h_hi, sc) * (int64_t)entropy_hi_weight_permille_env() + 500) / 1000);
                            if (c < best_c || (c == best_c && f < f_ent)) { best_c = c; f_ent = f; }
                        }
                        best_f = f_ent;
                    } else {
                        best_f = candidate_filters[0];
                        for (int ci = 1; ci < candidate_count; ci++) {
                            int f = candidate_filters[ci];
                            if (coarse_scores[f] < coarse_scores[best_f] || (coarse_scores[f] == coarse_scores[best_f] && f < best_f)) best_f = f;
                        }
                    }
                }
                if (stats && cost_model == FilterRowCostModel::LZCOST) {
                    if (best_f == 4) stats->filter_rows_lzcost_paeth_selected++;
                    if (best_f == 5) stats->filter_rows_lzcost_med_selected++;
                }
            } else {
                int64_t best_s = std::numeric_limits<int64_t>::max();
                int f_sad = candidate_filters[0];
                for (int ci = 0; ci < candidate_count; ci++) {
                    int f = candidate_filters[ci]; int64_t s = 0;
                    for (uint32_t x = 0; x < pad_w; x++) {
                        if (block_types[(size_t)by_row * nx + (x / 8)] != FileHeader::BlockType::DCT) continue;
                        const int16_t a = (x > 0) ? padded[(size_t)y * pad_w + (x - 1)] : 0;
                        const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * pad_w + x] : 0;
                        const int16_t c = (x > 0 && y > 0) ? padded[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
                        int16_t diff = (int16_t)((uint16_t)padded[(size_t)y * pad_w + x] - (uint16_t)LosslessFilter::predict((uint8_t)f, a, b, c));
                        if (cost_model == FilterRowCostModel::BITS2) s += lossless_mode_select::estimate_filter_symbol_bits2_fast(fast_abs(diff), bits_lut);
                        else s += fast_abs(diff);
                    }
                    if (s < best_s || (s == best_s && f < f_sad)) { best_s = s; f_sad = f; }
                }
                best_f = f_sad;
            }
        }

        filter_ids[y] = (uint8_t)best_f;
        if (stats) {
            stats->filter_rows_with_pixels++;
            if (best_f >= 0 && best_f < 8) stats->filter_row_id_hist[best_f]++;
            if (best_f == 5) stats->filter_med_selected++;
        }

        for (uint32_t x = 0; x < pad_w; x++) {
            if (block_types[(size_t)by_row * nx + (x / 8)] != FileHeader::BlockType::DCT) continue;
            const int16_t a = (x > 0) ? padded[(size_t)y * pad_w + (x - 1)] : 0;
            const int16_t b = (y > 0) ? padded[(size_t)(y - 1) * pad_w + x] : 0;
            const int16_t c = (x > 0 && y > 0) ? padded[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            filter_residuals.push_back((int16_t)((uint16_t)padded[(size_t)y * pad_w + x] - (uint16_t)LosslessFilter::predict((uint8_t)best_f, a, b, c)));
        }
    }
}

} // namespace hakonyans::lossless_filter_rows
