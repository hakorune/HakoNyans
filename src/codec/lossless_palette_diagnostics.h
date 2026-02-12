#pragma once

#include "lossless_mode_debug_stats.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hakonyans::lossless_palette_diagnostics {

inline void accumulate(
    const std::vector<uint8_t>& pal_raw,
    LosslessModeDebugStats& s
) {
    if (pal_raw.empty()) return;

    s.palette_stream_raw_bytes_sum += pal_raw.size();

    size_t pos = 0;
    bool is_v2 = false;
    bool is_v3 = false;
    bool is_v4 = false;
    uint8_t flags = 0;
    auto bits_for_palette_size = [](int p_size) -> int {
        if (p_size <= 1) return 0;
        if (p_size <= 2) return 1;
        if (p_size <= 4) return 2;
        return 3;
    };

    auto fail = [&]() {
        s.palette_parse_errors++;
        return;
    };

    if (pal_raw[0] == 0x40 || pal_raw[0] == 0x41 || pal_raw[0] == 0x42) {
        is_v2 = true;
        is_v3 = (pal_raw[0] == 0x41 || pal_raw[0] == 0x42);
        is_v4 = (pal_raw[0] == 0x42);
        if (is_v3) s.palette_stream_v3_count++;
        else s.palette_stream_v2_count++;
        pos = 1;
        if (pos >= pal_raw.size()) return fail();
        flags = pal_raw[pos++];

        if (flags & 0x01) {
            if (pos >= pal_raw.size()) return fail();
            uint8_t dict_count = pal_raw[pos++];
            s.palette_stream_mask_dict_count++;
            s.palette_stream_mask_dict_entries += dict_count;
            size_t need = (size_t)dict_count * 8;
            if (pos + need > pal_raw.size()) return fail();
            pos += need;
        }

        if (is_v3 && (flags & 0x02)) {
            if (pos >= pal_raw.size()) return fail();
            uint8_t pal_dict_count = pal_raw[pos++];
            s.palette_stream_palette_dict_count++;
            s.palette_stream_palette_dict_entries += pal_dict_count;
            for (uint8_t i = 0; i < pal_dict_count; i++) {
                if (pos >= pal_raw.size()) return fail();
                uint8_t psz = pal_raw[pos++];
                if (psz == 0 || psz > 8) return fail();
                size_t color_bytes = (size_t)psz * (is_v4 ? 2 : 1);
                if (pos + color_bytes > pal_raw.size()) return fail();
                pos += color_bytes;
            }
        }
    }

    while (pos < pal_raw.size()) {
        uint8_t head = pal_raw[pos++];
        bool use_prev = (head & 0x80) != 0;
        bool use_dict_ref = is_v3 && !use_prev && ((head & 0x40) != 0);
        int p_size = (head & 0x07) + 1;

        s.palette_blocks_parsed++;
        if (use_prev) s.palette_blocks_prev_reuse++;
        else if (use_dict_ref) s.palette_blocks_dict_ref++;
        else s.palette_blocks_raw_colors++;

        if (p_size <= 2) s.palette_blocks_two_color++;
        else s.palette_blocks_multi_color++;

        if (!use_prev) {
            if (use_dict_ref) {
                if (pos >= pal_raw.size()) return fail();
                pos += 1;
            } else {
                size_t color_bytes = (size_t)p_size * (is_v4 ? 2 : 1);
                if (pos + color_bytes > pal_raw.size()) return fail();
                pos += color_bytes;
            }
        }

        if (!is_v2 || p_size <= 1) continue;

        if (p_size == 2) {
            size_t need = (flags & 0x01) ? 1 : 8;
            if (pos + need > pal_raw.size()) return fail();
            pos += need;
            continue;
        }

        int bits = bits_for_palette_size(p_size);
        size_t idx_bytes = (size_t)((64 * bits + 7) / 8);
        if (pos + idx_bytes > pal_raw.size()) return fail();
        pos += idx_bytes;
    }
}

} // namespace hakonyans::lossless_palette_diagnostics
