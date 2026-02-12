#pragma once

#include "copy.h"
#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_mode_select.h"
#include "lossless_tile4_codec.h"
#include "palette.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace hakonyans::lossless_block_classifier {

struct ClassificationResult {
    std::vector<FileHeader::BlockType> block_types;
    std::vector<Palette> palettes;
    std::vector<std::vector<uint8_t>> palette_indices;
    std::vector<CopyParams> copy_ops;
    std::vector<lossless_tile4_codec::Tile4Result> tile4_results;
};

struct BlockEval {
    std::array<int16_t, 64> block{};
    int transitions = 0;
    int palette_transitions = 0;
    int unique_cnt = 0;
    int64_t variance_proxy = 0;

    bool copy_found = false;
    CopyParams copy_candidate{};
    bool palette_found = false;
    Palette palette_candidate{};
    std::vector<uint8_t> palette_index_candidate;
    bool tile4_found = false;
    lossless_tile4_codec::Tile4Result tile4_candidate{};

    int tile4_bits2 = std::numeric_limits<int>::max();
    int copy_bits2 = std::numeric_limits<int>::max();
    int palette_bits2 = std::numeric_limits<int>::max();
    int filter_bits2 = 0;

    int rescue_attempted_count = 0;
    bool rescue_adopted = false;
    uint64_t rescue_gain_bytes = 0;
    bool anime_palette_bonus_applied = false;
    bool rescue_bias_cond = false;
};

inline bool enable_filter_diag_palette16() {
    static const bool kEnabled = []() {
        const char* env = std::getenv("HKN_FILTER_DIAG_PALETTE16");
        if (!env || env[0] == '\0') return false;
        const char c = env[0];
        return (c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T');
    }();
    return kEnabled;
}

inline ClassificationResult classify_blocks(
    const std::vector<int16_t>& padded,
    uint32_t pad_w,
    uint32_t pad_h,
    int profile_id,
    LosslessModeDebugStats* stats
) {
    ClassificationResult out;

    const int nx = (int)(pad_w / 8);
    const int ny = (int)(pad_h / 8);
    const int nb = nx * ny;
    out.block_types.assign((size_t)nb, FileHeader::BlockType::DCT);

    const CopyParams kLosslessCopyCandidates[4] = {
        CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
    };

    const CopyParams kTileMatch4Candidates[16] = {
        CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
        CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
        CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
        CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
    };

    struct LosslessModeParams {
        int palette_max_colors = 2;
        int palette_transition_limit = 63;
        int64_t palette_variance_limit = 1040384;
    } mode_params;

    if (profile_id == 0) { // UI
        mode_params.palette_max_colors = 8;
        mode_params.palette_transition_limit = 58;
        mode_params.palette_variance_limit = 2621440;
    } else if (profile_id == 1) { // ANIME
        mode_params.palette_max_colors = 8;
        mode_params.palette_transition_limit = 62;
        mode_params.palette_variance_limit = 4194304;
    }

    auto evaluate_block = [&](int i) -> BlockEval {
        BlockEval ev;
        int bx = i % nx;
        int by = i / nx;
        int cur_x = bx * 8;
        int cur_y = by * 8;

        int64_t sum = 0;
        int64_t sum_sq = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int idx = y * 8 + x;
                int16_t v = padded[(size_t)(cur_y + y) * (size_t)pad_w + (size_t)(cur_x + x)];
                ev.block[(size_t)idx] = v;
                sum += v;
                sum_sq += (int64_t)v * (int64_t)v;
                if (idx > 0 && ev.block[(size_t)idx - 1] != v) ev.transitions++;
            }
        }

        {
            int16_t vals[64];
            std::memcpy(vals, ev.block.data(), sizeof(vals));
            std::sort(vals, vals + 64);
            ev.unique_cnt = 1;
            for (int k = 1; k < 64; k++) {
                if (vals[k] != vals[k - 1]) ev.unique_cnt++;
            }
        }

        ev.variance_proxy = sum_sq - ((sum * sum) / 64);

        ev.palette_transitions = ev.transitions;
        if (ev.unique_cnt <= mode_params.palette_max_colors) {
            ev.palette_candidate = PaletteExtractor::extract(ev.block.data(), mode_params.palette_max_colors);
            if (ev.palette_candidate.size > 0 && ev.palette_candidate.size <= mode_params.palette_max_colors) {
                bool transition_ok = (ev.transitions <= mode_params.palette_transition_limit) ||
                                     (ev.palette_candidate.size <= 1);
                bool variance_ok = ev.variance_proxy <= mode_params.palette_variance_limit;
                if (transition_ok && variance_ok) {
                    ev.palette_found = true;
                    ev.palette_index_candidate =
                        PaletteExtractor::map_indices(ev.block.data(), ev.palette_candidate);
                    ev.palette_transitions = 0;
                    for (int k = 1; k < 64; k++) {
                        if (ev.palette_index_candidate[(size_t)k] !=
                            ev.palette_index_candidate[(size_t)k - 1]) {
                            ev.palette_transitions++;
                        }
                    }
                }
            }
        }

        if (i > 0) {
            for (const auto& cand : kLosslessCopyCandidates) {
                int src_x = cur_x + cand.dx;
                int src_y = cur_y + cand.dy;
                if (src_x < 0 || src_y < 0) continue;
                if (src_x + 7 >= (int)pad_w || src_y + 7 >= (int)pad_h) continue;
                if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                bool match = true;
                for (int y = 0; y < 8; y++) {
                    const int16_t* dst_row =
                        &padded[(size_t)(cur_y + y) * (size_t)pad_w + (size_t)cur_x];
                    const int16_t* src_row =
                        &padded[(size_t)(src_y + y) * (size_t)pad_w + (size_t)src_x];
                    if (std::memcmp(dst_row, src_row, 8 * sizeof(int16_t)) != 0) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    ev.copy_found = true;
                    ev.copy_candidate = cand;
                    break;
                }
            }
        }

        {
            int matches = 0;
            for (int q = 0; q < 4; q++) {
                int qx = (q % 2) * 4;
                int qy = (q / 2) * 4;
                int cur_qx = cur_x + qx;
                int cur_qy = cur_y + qy;

                bool q_match_found = false;
                for (int cand_idx = 0; cand_idx < 16; cand_idx++) {
                    const auto& cand = kTileMatch4Candidates[cand_idx];
                    int src_x = cur_qx + cand.dx;
                    int src_y = cur_qy + cand.dy;

                    if (src_x < 0 || src_y < 0 || src_x + 3 >= (int)pad_w || src_y + 3 >= (int)pad_h) continue;
                    if (!(src_y < cur_qy || (src_y == cur_qy && src_x < cur_qx))) continue;

                    bool match = true;
                    for (int dy = 0; dy < 4; dy++) {
                        const int16_t* dst_row =
                            &padded[(size_t)(cur_qy + dy) * (size_t)pad_w + (size_t)cur_qx];
                        const int16_t* src_row =
                            &padded[(size_t)(src_y + dy) * (size_t)pad_w + (size_t)src_x];
                        if (std::memcmp(dst_row, src_row, 4 * sizeof(int16_t)) != 0) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        ev.tile4_candidate.indices[q] = (uint8_t)cand_idx;
                        q_match_found = true;
                        break;
                    }
                }
                if (q_match_found) matches++;
                else break;
            }
            ev.tile4_found = (matches == 4);
        }

        ev.filter_bits2 = lossless_mode_select::estimate_filter_bits(
            padded.data(), pad_w, pad_h, cur_x, cur_y, profile_id
        );

        if (ev.tile4_found) ev.tile4_bits2 = 36;
        if (ev.copy_found) {
            ev.copy_bits2 = lossless_mode_select::estimate_copy_bits(
                ev.copy_candidate, (int)pad_w, profile_id
            );
        }

        if (!ev.palette_found && profile_id != 2 && ev.unique_cnt <= 8) {
            Palette rescue_palette = PaletteExtractor::extract(ev.block.data(), 8);
            if (rescue_palette.size > 0 && rescue_palette.size <= 8) {
                ev.rescue_attempted_count++;
                auto rescue_indices = PaletteExtractor::map_indices(ev.block.data(), rescue_palette);
                int rescue_transitions = 0;
                for (int k = 1; k < 64; k++) {
                    if (rescue_indices[(size_t)k] != rescue_indices[(size_t)k - 1]) {
                        rescue_transitions++;
                    }
                }
                int rescue_bits2 = lossless_mode_select::estimate_palette_bits(
                    rescue_palette, rescue_transitions, profile_id
                );
                if (profile_id == 1 && rescue_palette.size >= 2 && rescue_transitions <= 60) {
                    rescue_bits2 -= 24;
                }
                if (rescue_bits2 + 8 < ev.filter_bits2) {
                    ev.palette_found = true;
                    ev.palette_candidate = rescue_palette;
                    ev.palette_index_candidate = std::move(rescue_indices);
                    ev.palette_transitions = rescue_transitions;
                    ev.rescue_adopted = true;
                    ev.rescue_gain_bytes =
                        (uint64_t)std::max(0, (ev.filter_bits2 - rescue_bits2) / 2);
                }
            }
        }

        if (ev.palette_found) {
            ev.palette_bits2 = lossless_mode_select::estimate_palette_bits(
                ev.palette_candidate, ev.palette_transitions, profile_id
            );
            if (profile_id == 1 && ev.palette_candidate.size >= 2 && ev.palette_transitions <= 60) {
                ev.palette_bits2 -= 24;
                ev.anime_palette_bonus_applied = true;
            }

            ev.rescue_bias_cond =
                (profile_id != 2) &&
                (ev.palette_candidate.size <= 8) &&
                (ev.unique_cnt <= 8) &&
                (ev.palette_transitions <= 32) &&
                (ev.variance_proxy >= 30000);
            if (ev.rescue_bias_cond) {
                ev.rescue_attempted_count++;
                ev.palette_bits2 -= 32;
            }
        }

        return ev;
    };

    std::vector<BlockEval> evals((size_t)nb);
    const unsigned int hw_threads = std::max(1u, std::thread::hardware_concurrency());
    const bool use_parallel_eval = (hw_threads >= 2 && nb >= 256);
    if (use_parallel_eval) {
        int task_count = std::min<int>((int)hw_threads, std::max(1, nb / 64));
        task_count = std::max(1, task_count);
        int chunk = (nb + task_count - 1) / task_count;
        std::vector<std::future<void>> futs;
        futs.reserve((size_t)task_count);
        for (int t = 0; t < task_count; t++) {
            int begin = t * chunk;
            int end = std::min(nb, begin + chunk);
            if (begin >= end) continue;
            futs.push_back(std::async(std::launch::async, [&, begin, end]() {
                for (int j = begin; j < end; j++) {
                    evals[(size_t)j] = evaluate_block(j);
                }
            }));
        }
        for (auto& f : futs) f.get();
    } else {
        for (int i = 0; i < nb; i++) {
            evals[(size_t)i] = evaluate_block(i);
        }
    }

    const bool diag_palette16 = enable_filter_diag_palette16();
    FileHeader::BlockType prev_mode = FileHeader::BlockType::DCT;

    for (int i = 0; i < nb; i++) {
        auto& ev = evals[(size_t)i];

        int tile4_bits2 = ev.tile4_bits2;
        int copy_bits2 = ev.copy_bits2;
        int palette_bits2 = ev.palette_bits2;
        int filter_bits2 = ev.filter_bits2;

        if (profile_id == 2) {
            if (ev.tile4_found && prev_mode == FileHeader::BlockType::TILE_MATCH4) tile4_bits2 -= 4;
            if (ev.copy_found && prev_mode == FileHeader::BlockType::COPY) copy_bits2 -= 4;
            if (ev.palette_found && prev_mode == FileHeader::BlockType::PALETTE) palette_bits2 -= 4;
            if (prev_mode == FileHeader::BlockType::DCT) filter_bits2 -= 4;
        }

        if (stats) {
            stats->palette_rescue_attempted += (uint64_t)ev.rescue_attempted_count;
            if (ev.rescue_adopted) {
                stats->palette_rescue_adopted++;
                stats->palette_rescue_gain_bits_sum += ev.rescue_gain_bytes;
            }
            if (ev.anime_palette_bonus_applied) {
                stats->anime_palette_bonus_applied++;
            }

            stats->total_blocks++;
            stats->est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);
            if (ev.tile4_found) {
                stats->tile4_candidates++;
                stats->est_tile4_bits_sum += (uint64_t)(tile4_bits2 / 2);
            }
            if (ev.copy_found) {
                stats->copy_candidates++;
                stats->est_copy_bits_sum += (uint64_t)(copy_bits2 / 2);
            }
            if (ev.palette_found) {
                stats->palette_candidates++;
                stats->est_palette_bits_sum += (uint64_t)(palette_bits2 / 2);
            }
            if (ev.copy_found && ev.palette_found) stats->copy_palette_overlap++;
        }

        FileHeader::BlockType best_mode = FileHeader::BlockType::DCT;
        if (tile4_bits2 <= copy_bits2 && tile4_bits2 <= palette_bits2 && tile4_bits2 <= filter_bits2) {
            best_mode = FileHeader::BlockType::TILE_MATCH4;
        } else if (copy_bits2 <= palette_bits2 && copy_bits2 <= filter_bits2) {
            best_mode = FileHeader::BlockType::COPY;
        } else if (palette_bits2 <= filter_bits2) {
            best_mode = FileHeader::BlockType::PALETTE;
        }

        int selected_bits2 = filter_bits2;
        if (best_mode == FileHeader::BlockType::TILE_MATCH4) selected_bits2 = tile4_bits2;
        else if (best_mode == FileHeader::BlockType::COPY) selected_bits2 = copy_bits2;
        else if (best_mode == FileHeader::BlockType::PALETTE) selected_bits2 = palette_bits2;

        if (stats) {
            if (ev.tile4_found && best_mode != FileHeader::BlockType::TILE_MATCH4) {
                if (best_mode == FileHeader::BlockType::COPY) stats->tile4_rejected_by_copy++;
                else if (best_mode == FileHeader::BlockType::PALETTE) stats->tile4_rejected_by_palette++;
                else stats->tile4_rejected_by_filter++;
                stats->est_tile4_loss_bits_sum +=
                    (uint64_t)(std::max(0, tile4_bits2 - selected_bits2) / 2);
            }
            if (ev.copy_found && best_mode != FileHeader::BlockType::COPY) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) stats->copy_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::PALETTE) stats->copy_rejected_by_palette++;
                else stats->copy_rejected_by_filter++;
                stats->est_copy_loss_bits_sum +=
                    (uint64_t)(std::max(0, copy_bits2 - selected_bits2) / 2);
            }
            if (ev.palette_found && best_mode != FileHeader::BlockType::PALETTE) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) stats->palette_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::COPY) stats->palette_rejected_by_copy++;
                else stats->palette_rejected_by_filter++;
                stats->est_palette_loss_bits_sum +=
                    (uint64_t)(std::max(0, palette_bits2 - selected_bits2) / 2);
            }
        }

        out.block_types[(size_t)i] = best_mode;
        prev_mode = best_mode;
        if (best_mode == FileHeader::BlockType::TILE_MATCH4) {
            out.tile4_results.push_back(ev.tile4_candidate);
            if (stats) {
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                stats->tile4_selected++;
            }
        } else if (best_mode == FileHeader::BlockType::COPY) {
            out.copy_ops.push_back(ev.copy_candidate);
            if (stats) {
                stats->copy_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
            }
        } else if (best_mode == FileHeader::BlockType::PALETTE) {
            out.palettes.push_back(ev.palette_candidate);
            out.palette_indices.push_back(std::move(ev.palette_index_candidate));
            if (stats) {
                stats->palette_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                if (ev.rescue_bias_cond) {
                    stats->palette_rescue_adopted++;
                    stats->palette_rescue_gain_bits_sum += 16;
                }
            }
        } else {
            if (stats) {
                stats->filter_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);

                if (ev.copy_found) stats->filter_blocks_with_copy_candidate++;
                if (ev.palette_found) stats->filter_blocks_with_palette_candidate++;
                if (ev.unique_cnt <= 2) stats->filter_blocks_unique_le2++;
                else if (ev.unique_cnt <= 4) stats->filter_blocks_unique_le4++;
                else if (ev.unique_cnt <= 8) stats->filter_blocks_unique_le8++;
                else stats->filter_blocks_unique_gt8++;
                stats->filter_blocks_transitions_sum += (uint64_t)ev.transitions;
                stats->filter_blocks_variance_proxy_sum +=
                    (uint64_t)std::max<int64_t>(0, ev.variance_proxy);
                stats->filter_blocks_est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);

                if (diag_palette16 && ev.unique_cnt <= 8) {
                    Palette diag_palette16_res = PaletteExtractor::extract(ev.block.data(), 8);
                    if (diag_palette16_res.size > 0 && diag_palette16_res.size <= 8) {
                        auto diag_indices =
                            PaletteExtractor::map_indices(ev.block.data(), diag_palette16_res);
                        int diag_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (diag_indices[(size_t)k] != diag_indices[(size_t)k - 1]) {
                                diag_transitions++;
                            }
                        }
                        int diag_palette_bits2 = lossless_mode_select::estimate_palette_bits(
                            diag_palette16_res, diag_transitions, profile_id
                        );
                        if (profile_id == 1 &&
                            diag_palette16_res.size >= 2 && diag_transitions <= 60) {
                            diag_palette_bits2 -= 24;
                        }
                        stats->filter_diag_palette16_candidates++;
                        stats->filter_diag_palette16_size_sum += diag_palette16_res.size;
                        stats->filter_diag_palette16_est_bits_sum +=
                            (uint64_t)(diag_palette_bits2 / 2);
                        if (diag_palette_bits2 < filter_bits2) {
                            stats->filter_diag_palette16_better++;
                            stats->filter_diag_palette16_gain_bits_sum +=
                                (uint64_t)((filter_bits2 - diag_palette_bits2) / 2);
                        }
                    }
                }
            }
        }
    }

    return out;
}

} // namespace hakonyans::lossless_block_classifier
