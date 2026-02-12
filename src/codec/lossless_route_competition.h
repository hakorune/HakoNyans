#pragma once

#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_screen_route.h"
#include <cstddef>
#include <cstdint>
#include <future>
#include <thread>
#include <utility>
#include <vector>

namespace hakonyans::lossless_route_competition {

enum class ExtraRoute : uint8_t { LEGACY = 0, SCREEN = 1, NATURAL = 2 };

template <typename AnalyzeScreenPreflightFn,
          typename EncodeScreenTileFn,
          typename IsNaturalLikeFn,
          typename EncodeNaturalTileFn>
inline std::vector<uint8_t> choose_best_tile(
    const std::vector<uint8_t>& legacy_tile,
    const int16_t* data,
    uint32_t width,
    uint32_t height,
    int profile_id,
    LosslessModeDebugStats* stats,
    AnalyzeScreenPreflightFn&& analyze_screen_preflight,
    EncodeScreenTileFn&& encode_screen_tile,
    IsNaturalLikeFn&& is_natural_like,
    EncodeNaturalTileFn&& encode_natural_tile
) {
    if (!stats) {
        return legacy_tile;
    }

    std::vector<uint8_t> best_tile = legacy_tile;
    ExtraRoute chosen_route = ExtraRoute::LEGACY;
    const size_t legacy_size = legacy_tile.size();

    size_t selected_screen_size = 0;
    using ScreenPreflightMetrics = lossless_screen_route::ScreenPreflightMetrics;
    using ScreenBuildFailReason = lossless_screen_route::ScreenBuildFailReason;

    bool screen_prefilter_valid = false;
    bool screen_prefilter_likely_screen = false;
    bool natural_prefilter_ok = false;
    const bool large_image = ((uint64_t)width * (uint64_t)height >= 262144ull);
    stats->screen_candidate_count++;

    if (width * height >= 4096) {
        const auto pre = analyze_screen_preflight(data, width, height);
        screen_prefilter_valid = true;
        screen_prefilter_likely_screen = pre.likely_screen;
        stats->screen_prefilter_eval_count++;
        stats->screen_prefilter_unique_sum += pre.unique_sample;
        stats->screen_prefilter_avg_run_x100_sum += pre.avg_run_x100;
        stats->natural_prefilter_eval_count++;
        stats->natural_prefilter_unique_sum += pre.unique_sample;
        stats->natural_prefilter_avg_run_x100_sum += pre.avg_run_x100;
        stats->natural_prefilter_mad_x100_sum += pre.mean_abs_diff_x100;
        stats->natural_prefilter_entropy_x100_sum += pre.run_entropy_hint_x100;
        natural_prefilter_ok = is_natural_like(pre);
        if (natural_prefilter_ok) stats->natural_prefilter_pass_count++;
        else stats->natural_prefilter_reject_count++;
    }

    const bool screen_like = screen_prefilter_valid && screen_prefilter_likely_screen;
    const bool natural_like = screen_prefilter_valid && natural_prefilter_ok;
    const bool skip_screen_for_natural = natural_like && profile_id == 2;
    const bool allow_screen_route =
        (width * height >= 4096) && screen_prefilter_likely_screen && !skip_screen_for_natural;

    if (width * height < 4096) {
        stats->screen_rejected_pre_gate++;
        stats->screen_rejected_small_tile++;
    } else if (!allow_screen_route) {
        stats->screen_rejected_pre_gate++;
        stats->screen_rejected_prefilter_texture++;
    }

    size_t natural_size = 0;
    const bool allow_natural_route = natural_like || (profile_id == 2 && !screen_like);
    const bool try_natural_route = large_image && allow_natural_route;
    const bool can_parallel_compete = allow_screen_route && try_natural_route &&
                                      (std::max(1u, std::thread::hardware_concurrency()) >= 2);

    struct ScreenCandidateResult {
        bool attempted = false;
        ScreenBuildFailReason fail_reason = ScreenBuildFailReason::NONE;
        std::vector<uint8_t> tile;
    };
    struct NaturalCandidateResult {
        bool attempted = false;
        std::vector<uint8_t> tile;
    };

    auto run_screen_candidate = [&]() -> ScreenCandidateResult {
        ScreenCandidateResult out;
        out.attempted = allow_screen_route;
        if (!out.attempted) return out;
        out.tile = encode_screen_tile(data, width, height, &out.fail_reason);
        return out;
    };
    auto run_natural_candidate = [&]() -> NaturalCandidateResult {
        NaturalCandidateResult out;
        out.attempted = try_natural_route;
        if (!out.attempted) return out;
        out.tile = encode_natural_tile(data, width, height);
        return out;
    };

    ScreenCandidateResult screen_res;
    NaturalCandidateResult natural_res;
    if (can_parallel_compete) {
        auto f_screen = std::async(std::launch::async, run_screen_candidate);
        auto f_natural = std::async(std::launch::async, run_natural_candidate);
        screen_res = f_screen.get();
        natural_res = f_natural.get();
    } else {
        screen_res = run_screen_candidate();
        natural_res = run_natural_candidate();
    }

    if (screen_res.attempted) {
        auto& screen_tile = screen_res.tile;
        if (screen_tile.empty() || screen_tile.size() < 14) {
            stats->screen_rejected_pre_gate++;
            stats->screen_rejected_build_fail++;
            if (screen_res.fail_reason == ScreenBuildFailReason::TOO_MANY_UNIQUE) {
                stats->screen_build_fail_too_many_unique++;
            } else if (screen_res.fail_reason == ScreenBuildFailReason::EMPTY_HIST) {
                stats->screen_build_fail_empty_hist++;
            } else if (screen_res.fail_reason == ScreenBuildFailReason::INDEX_MISS) {
                stats->screen_build_fail_index_miss++;
            } else {
                stats->screen_build_fail_other++;
            }
        } else {
            uint8_t screen_mode = screen_tile[1];
            uint16_t palette_count = (uint16_t)screen_tile[4] | ((uint16_t)screen_tile[5] << 8);
            uint32_t packed_size = (uint32_t)screen_tile[10] | ((uint32_t)screen_tile[11] << 8) |
                                   ((uint32_t)screen_tile[12] << 16) | ((uint32_t)screen_tile[13] << 24);

            stats->screen_palette_count_sum += palette_count;

            int bits_per_index = 0;
            if (palette_count <= 2) bits_per_index = 1;
            else if (palette_count <= 4) bits_per_index = 2;
            else if (palette_count <= 16) bits_per_index = 4;
            else if (palette_count <= 64) bits_per_index = 6;
            else bits_per_index = 8;
            stats->screen_bits_per_index_sum += bits_per_index;

            bool reject_strict = false;
            if (palette_count > 64) {
                reject_strict = true;
                stats->screen_rejected_palette_limit++;
            }
            if (bits_per_index > 6) {
                reject_strict = true;
                stats->screen_rejected_bits_limit++;
            }

            if (reject_strict) {
                stats->screen_rejected_pre_gate++;
            } else {
                const size_t screen_size = screen_tile.size();
                stats->screen_compete_legacy_bytes_sum += legacy_size;
                stats->screen_compete_screen_bytes_sum += screen_size;

                bool is_ui_like = (palette_count <= 24 && bits_per_index <= 5);
                if (is_ui_like) stats->screen_ui_like_count++;
                else stats->screen_anime_like_count++;

                int gate_permille = 1000;
                if (profile_id == 0) gate_permille = 995;
                else if (profile_id == 1) gate_permille = 990;

                bool adopt = (screen_size * 1000ull <= legacy_size * (uint64_t)gate_permille);
                if (adopt) {
                    selected_screen_size = screen_size;
                    if (screen_size < best_tile.size()) {
                        best_tile = std::move(screen_tile);
                        chosen_route = ExtraRoute::SCREEN;
                    }
                } else {
                    stats->screen_rejected_cost_gate++;
                    if (screen_size > legacy_size) {
                        stats->screen_loss_bytes_sum += (screen_size - legacy_size);
                    }
                    if (screen_mode == 0 && packed_size > 2048) {
                        stats->screen_mode0_reject_count++;
                    }
                }
            }
        }
    }

    if (natural_res.attempted) {
        stats->natural_row_candidate_count++;
        auto& natural_tile = natural_res.tile;
        if (natural_tile.empty()) {
            stats->natural_row_build_fail_count++;
        } else {
            natural_size = natural_tile.size();
            if (natural_size <= legacy_size) {
                if (natural_size < best_tile.size()) {
                    best_tile = std::move(natural_tile);
                    chosen_route = ExtraRoute::NATURAL;
                }
            } else {
                stats->natural_row_rejected_cost_gate++;
                stats->natural_row_loss_bytes_sum += (natural_size - legacy_size);
            }
        }
    }

    if (chosen_route == ExtraRoute::NATURAL) {
        stats->natural_row_selected_count++;
        if (legacy_size > natural_size) {
            stats->natural_row_gain_bytes_sum += (legacy_size - natural_size);
        }
    } else if (chosen_route == ExtraRoute::SCREEN) {
        stats->screen_selected_count++;
        if (legacy_size > selected_screen_size) {
            stats->screen_gain_bytes_sum += (legacy_size - selected_screen_size);
        }
    }

    return best_tile;
}

} // namespace hakonyans::lossless_route_competition
