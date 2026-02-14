#pragma once

#include "copy.h"
#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_mode_select.h"
#include "lossless_tile4_codec.h"
#include "palette.h"
#include "../platform/thread_budget.h"
#include "../platform/thread_pool.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <future>
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

struct BlockEval {
    std::array<int16_t, 64> block{};
    int transitions = 0;
    int palette_transitions = 0;
    int unique_cnt = 0;
    int64_t variance_proxy = 0;

    bool copy_found = false;
    bool copy_shortcut_forced = false;
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
    #include "lossless_block_classifier_eval_setup.inc"
    #include "lossless_block_classifier_eval_parallel.inc"
    #include "lossless_block_classifier_eval_select.inc"
}

} // namespace hakonyans::lossless_block_classifier
