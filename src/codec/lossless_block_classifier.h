#pragma once

#include "copy.h"
#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_mode_select.h"
#include "lossless_tile4_codec.h"
#include "palette.h"
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
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

    FileHeader::BlockType prev_mode = FileHeader::BlockType::DCT;

    for (int i = 0; i < nb; i++) {
        int bx = i % nx;
        int by = i / nx;
        int cur_x = bx * 8;
        int cur_y = by * 8;

        int16_t block[64];
        int64_t sum = 0, sum_sq = 0;
        int transitions = 0;
        int palette_transitions = 0;
        int unique_cnt = 0;

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int idx = y * 8 + x;
                int16_t v = padded[(size_t)(cur_y + y) * (size_t)pad_w + (size_t)(cur_x + x)];
                block[idx] = v;
                sum += v;
                sum_sq += (int64_t)v * (int64_t)v;
                if (idx > 0 && block[idx - 1] != v) transitions++;
            }
        }

        {
            int16_t vals[64];
            std::memcpy(vals, block, sizeof(vals));
            std::sort(vals, vals + 64);
            unique_cnt = 1;
            for (int k = 1; k < 64; k++) {
                if (vals[k] != vals[k - 1]) unique_cnt++;
            }
        }

        int64_t variance_proxy = sum_sq - ((sum * sum) / 64);

        bool copy_found = false;
        CopyParams copy_candidate;
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
                    copy_found = true;
                    copy_candidate = cand;
                    break;
                }
            }
        }

        bool palette_found = false;
        Palette palette_candidate;
        std::vector<uint8_t> palette_index_candidate;
        palette_transitions = transitions;
        if (unique_cnt <= mode_params.palette_max_colors) {
            palette_candidate = PaletteExtractor::extract(block, mode_params.palette_max_colors);
            if (palette_candidate.size > 0 && palette_candidate.size <= mode_params.palette_max_colors) {
                bool transition_ok = (transitions <= mode_params.palette_transition_limit) ||
                                     (palette_candidate.size <= 1);
                bool variance_ok = variance_proxy <= mode_params.palette_variance_limit;
                if (transition_ok && variance_ok) {
                    palette_found = true;
                    palette_index_candidate = PaletteExtractor::map_indices(block, palette_candidate);
                    palette_transitions = 0;
                    for (int k = 1; k < 64; k++) {
                        if (palette_index_candidate[(size_t)k] != palette_index_candidate[(size_t)k - 1]) {
                            palette_transitions++;
                        }
                    }
                }
            }
        }

        bool tile4_found = false;
        lossless_tile4_codec::Tile4Result tile4_candidate;
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
                        tile4_candidate.indices[q] = (uint8_t)cand_idx;
                        q_match_found = true;
                        break;
                    }
                }
                if (q_match_found) matches++;
                else break;
            }
            if (matches == 4) tile4_found = true;
        }

        int tile4_bits2 = std::numeric_limits<int>::max();
        int copy_bits2 = std::numeric_limits<int>::max();
        int palette_bits2 = std::numeric_limits<int>::max();
        int filter_bits2 = lossless_mode_select::estimate_filter_bits(
            padded.data(), pad_w, pad_h, cur_x, cur_y, profile_id
        );

        if (tile4_found) tile4_bits2 = 36;
        if (copy_found) {
            copy_bits2 = lossless_mode_select::estimate_copy_bits(copy_candidate, (int)pad_w, profile_id);
        }

        if (!palette_found && profile_id != 2 && unique_cnt <= 8) {
            Palette rescue_palette = PaletteExtractor::extract(block, 8);
            if (rescue_palette.size > 0 && rescue_palette.size <= 8) {
                if (stats) stats->palette_rescue_attempted++;
                auto rescue_indices = PaletteExtractor::map_indices(block, rescue_palette);
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
                if (rescue_bits2 + 8 < filter_bits2) {
                    palette_found = true;
                    palette_candidate = rescue_palette;
                    palette_index_candidate = std::move(rescue_indices);
                    palette_transitions = rescue_transitions;
                    if (stats) {
                        stats->palette_rescue_adopted++;
                        stats->palette_rescue_gain_bits_sum +=
                            (uint64_t)std::max(0, (filter_bits2 - rescue_bits2) / 2);
                    }
                }
            }
        }

        if (palette_found) {
            palette_bits2 = lossless_mode_select::estimate_palette_bits(
                palette_candidate, palette_transitions, profile_id
            );
            if (profile_id == 1 && palette_candidate.size >= 2 && palette_transitions <= 60) {
                palette_bits2 -= 24;
                if (stats) stats->anime_palette_bonus_applied++;
            }

            const bool rescue_bias_cond =
                (profile_id != 2) &&
                (palette_candidate.size <= 8) &&
                (unique_cnt <= 8) &&
                (palette_transitions <= 32) &&
                (variance_proxy >= 30000);
            if (rescue_bias_cond) {
                if (stats) stats->palette_rescue_attempted++;
                palette_bits2 -= 32;
            }
        }

        if (profile_id == 2) {
            if (tile4_found && prev_mode == FileHeader::BlockType::TILE_MATCH4) tile4_bits2 -= 4;
            if (copy_found && prev_mode == FileHeader::BlockType::COPY) copy_bits2 -= 4;
            if (palette_found && prev_mode == FileHeader::BlockType::PALETTE) palette_bits2 -= 4;
            if (prev_mode == FileHeader::BlockType::DCT) filter_bits2 -= 4;
        }

        if (stats) {
            stats->total_blocks++;
            stats->est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);
            if (tile4_found) {
                stats->tile4_candidates++;
                stats->est_tile4_bits_sum += (uint64_t)(tile4_bits2 / 2);
            }
            if (copy_found) {
                stats->copy_candidates++;
                stats->est_copy_bits_sum += (uint64_t)(copy_bits2 / 2);
            }
            if (palette_found) {
                stats->palette_candidates++;
                stats->est_palette_bits_sum += (uint64_t)(palette_bits2 / 2);
            }
            if (copy_found && palette_found) stats->copy_palette_overlap++;
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
            if (tile4_found && best_mode != FileHeader::BlockType::TILE_MATCH4) {
                if (best_mode == FileHeader::BlockType::COPY) stats->tile4_rejected_by_copy++;
                else if (best_mode == FileHeader::BlockType::PALETTE) stats->tile4_rejected_by_palette++;
                else stats->tile4_rejected_by_filter++;
                stats->est_tile4_loss_bits_sum +=
                    (uint64_t)(std::max(0, tile4_bits2 - selected_bits2) / 2);
            }
            if (copy_found && best_mode != FileHeader::BlockType::COPY) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) stats->copy_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::PALETTE) stats->copy_rejected_by_palette++;
                else stats->copy_rejected_by_filter++;
                stats->est_copy_loss_bits_sum +=
                    (uint64_t)(std::max(0, copy_bits2 - selected_bits2) / 2);
            }
            if (palette_found && best_mode != FileHeader::BlockType::PALETTE) {
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
            out.tile4_results.push_back(tile4_candidate);
            if (stats) {
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                stats->tile4_selected++;
            }
        } else if (best_mode == FileHeader::BlockType::COPY) {
            out.copy_ops.push_back(copy_candidate);
            if (stats) {
                stats->copy_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
            }
        } else if (best_mode == FileHeader::BlockType::PALETTE) {
            out.palettes.push_back(palette_candidate);
            out.palette_indices.push_back(std::move(palette_index_candidate));
            if (stats) {
                stats->palette_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                if (profile_id != 2 &&
                    palette_candidate.size <= 8 &&
                    unique_cnt <= 8 &&
                    palette_transitions <= 32 &&
                    variance_proxy >= 30000) {
                    stats->palette_rescue_adopted++;
                    stats->palette_rescue_gain_bits_sum += 16;
                }
            }
        } else {
            if (stats) {
                stats->filter_selected++;
                stats->est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);

                if (copy_found) stats->filter_blocks_with_copy_candidate++;
                if (palette_found) stats->filter_blocks_with_palette_candidate++;
                if (unique_cnt <= 2) stats->filter_blocks_unique_le2++;
                else if (unique_cnt <= 4) stats->filter_blocks_unique_le4++;
                else if (unique_cnt <= 8) stats->filter_blocks_unique_le8++;
                else stats->filter_blocks_unique_gt8++;
                stats->filter_blocks_transitions_sum += (uint64_t)transitions;
                stats->filter_blocks_variance_proxy_sum +=
                    (uint64_t)std::max<int64_t>(0, variance_proxy);
                stats->filter_blocks_est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);

                if (enable_filter_diag_palette16() && unique_cnt <= 8) {
                    Palette diag_palette16 = PaletteExtractor::extract(block, 8);
                    if (diag_palette16.size > 0 && diag_palette16.size <= 8) {
                        auto diag_indices = PaletteExtractor::map_indices(block, diag_palette16);
                        int diag_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (diag_indices[(size_t)k] != diag_indices[(size_t)k - 1]) {
                                diag_transitions++;
                            }
                        }
                        int diag_palette_bits2 = lossless_mode_select::estimate_palette_bits(
                            diag_palette16, diag_transitions, profile_id
                        );
                        if (profile_id == 1 &&
                            diag_palette16.size >= 2 && diag_transitions <= 60) {
                            diag_palette_bits2 -= 24;
                        }
                        stats->filter_diag_palette16_candidates++;
                        stats->filter_diag_palette16_size_sum += diag_palette16.size;
                        stats->filter_diag_palette16_est_bits_sum += (uint64_t)(diag_palette_bits2 / 2);
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
