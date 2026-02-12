#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace hakonyans::lossless_screen {

struct PreflightMetrics {
    uint16_t unique_sample = 0;
    uint16_t avg_run_x100 = 0;
    uint16_t mean_abs_diff_x100 = 0;
    uint16_t run_entropy_hint_x100 = 0;
    bool likely_screen = false;
};

inline int bits_for_symbol_count(int count) {
    if (count <= 1) return 0;
    int bits = 0;
    int v = 1;
    while (v < count) {
        v <<= 1;
        bits++;
    }
    return bits;
}

inline std::vector<uint8_t> pack_index_bits(const std::vector<uint8_t>& indices, int bits) {
    std::vector<uint8_t> out;
    if (bits <= 0 || indices.empty()) return out;
    out.reserve((indices.size() * (size_t)bits + 7) / 8);
    uint64_t acc = 0;
    int acc_bits = 0;
    const uint32_t mask = (1u << bits) - 1u;
    for (uint8_t idx : indices) {
        acc |= (uint64_t)((uint32_t)idx & mask) << acc_bits;
        acc_bits += bits;
        while (acc_bits >= 8) {
            out.push_back((uint8_t)(acc & 0xFFu));
            acc >>= 8;
            acc_bits -= 8;
        }
    }
    if (acc_bits > 0) out.push_back((uint8_t)(acc & 0xFFu));
    return out;
}

inline PreflightMetrics analyze_preflight(const int16_t* plane, uint32_t width, uint32_t height) {
    PreflightMetrics m;
    if (!plane || width == 0 || height == 0) return m;

    const uint32_t sx = std::max<uint32_t>(1, width / 64);
    const uint32_t sy = std::max<uint32_t>(1, height / 64);
    std::unordered_set<int16_t> uniq;
    uniq.reserve(128);
    for (uint32_t y = 0; y < height; y += sy) {
        const int16_t* row = plane + (size_t)y * width;
        for (uint32_t x = 0; x < width; x += sx) {
            uniq.insert(row[x]);
            if (uniq.size() > 192) break;
        }
        if (uniq.size() > 192) break;
    }
    m.unique_sample = (uint16_t)std::min<size_t>(uniq.size(), 65535);

    uint32_t sampled_rows = std::min<uint32_t>(height, 32);
    uint32_t row_step = std::max<uint32_t>(1, height / std::max<uint32_t>(1, sampled_rows));
    uint64_t total_pixels = 0;
    uint64_t total_runs = 0;
    uint64_t total_abs_diff = 0;
    uint64_t total_diffs = 0;
    for (uint32_t y = 0; y < height; y += row_step) {
        const int16_t* row = plane + (size_t)y * width;
        if (width == 0) continue;
        total_runs += 1;
        total_pixels += width;
        int16_t prev = row[0];
        for (uint32_t x = 1; x < width; x++) {
            int16_t v = row[x];
            total_abs_diff += (uint64_t)std::abs((int)v - (int)prev);
            total_diffs++;
            if (v != prev) {
                total_runs++;
                prev = v;
            }
        }
    }
    double avg_run = (total_runs > 0) ? ((double)total_pixels / (double)total_runs) : 0.0;
    m.avg_run_x100 = (uint16_t)std::clamp<int>((int)std::lround(avg_run * 100.0), 0, 65535);
    double mean_abs_diff = (total_diffs > 0) ? ((double)total_abs_diff / (double)total_diffs) : 0.0;
    m.mean_abs_diff_x100 =
        (uint16_t)std::clamp<int>((int)std::lround(mean_abs_diff * 100.0), 0, 65535);
    double entropy_hint = (total_pixels > 0) ? ((double)total_runs / (double)total_pixels) : 0.0;
    m.run_entropy_hint_x100 =
        (uint16_t)std::clamp<int>((int)std::lround(entropy_hint * 100.0), 0, 65535);

    if (m.unique_sample <= 48) m.likely_screen = true;
    else if (m.unique_sample <= 96 && m.avg_run_x100 >= 280) m.likely_screen = true;
    else m.likely_screen = false;
    // Keep low-color UI content as screen-like even when edges are sharp.
    if (m.mean_abs_diff_x100 >= 2200 && m.unique_sample > 96) m.likely_screen = false;
    return m;
}

} // namespace hakonyans::lossless_screen
