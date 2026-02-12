#pragma once

#include "copy.h"
#include "lossless_mode_debug_stats.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hakonyans::lossless_stream_diagnostics {

inline void accumulate(
    LosslessModeDebugStats& s,
    const std::vector<uint8_t>& bt_data,
    const std::vector<uint8_t>& palette_data,
    const std::vector<uint8_t>& tile4_data,
    size_t tile4_raw_size,
    const std::vector<CopyParams>& copy_ops,
    const std::vector<uint8_t>& copy_raw,
    const std::vector<uint8_t>& copy_wrapped,
    int copy_wrapper_mode
) {
    s.block_types_bytes_sum += bt_data.size();
    s.palette_stream_bytes_sum += palette_data.size();
    s.tile4_stream_bytes_sum += tile4_data.size();

    for (uint8_t v : bt_data) {
        int run = ((v >> 2) & 0x3F) + 1;
        uint8_t type = (v & 0x03);
        s.block_type_runs_sum++;
        if (run <= 2) s.block_type_short_runs++;
        if (run >= 16) s.block_type_long_runs++;
        switch (type) {
            case 0: s.block_type_runs_dct++; break;
            case 1: s.block_type_runs_palette++; break;
            case 2: s.block_type_runs_copy++; break;
            case 3: s.block_type_runs_tile4++; break;
            default: break;
        }
    }

    s.copy_stream_bytes_sum += copy_wrapped.size();
    s.copy_ops_total += copy_ops.size();
    for (const auto& cp : copy_ops) {
        if (CopyCodec::small_vector_index(cp) >= 0) s.copy_ops_small++;
        else s.copy_ops_raw++;
    }
    if (!copy_ops.empty()) {
        if (copy_wrapper_mode == 1) s.copy_wrap_mode1++;
        else if (copy_wrapper_mode == 2) s.copy_wrap_mode2++;
        else s.copy_wrap_mode0++;
    }

    if (!copy_ops.empty() && !copy_raw.empty()) {
        s.copy_stream_count++;
        uint8_t mode = copy_raw[0];
        uint64_t payload_bits = 0;
        if (mode == 0) {
            s.copy_stream_mode0++;
            payload_bits = (uint64_t)copy_ops.size() * 32ull;
        } else if (mode == 1) {
            s.copy_stream_mode1++;
            payload_bits = (uint64_t)copy_ops.size() * 2ull;
        } else if (mode == 2) {
            s.copy_stream_mode2++;
            if (copy_raw.size() >= 2) {
                uint8_t used_mask = copy_raw[1];
                int used_count = CopyCodec::popcount4(used_mask);
                int bits_dyn = CopyCodec::small_vector_bits(used_count);
                if (bits_dyn == 0) s.copy_mode2_zero_bit_streams++;
                s.copy_mode2_dynamic_bits_sum += (uint64_t)bits_dyn;
                payload_bits = (uint64_t)copy_ops.size() * (uint64_t)std::max(0, bits_dyn);
            }
        } else if (mode == 3) {
            s.copy_stream_mode3++;
            if (copy_raw.size() >= 2) {
                size_t num_tokens = copy_raw.size() - 2;
                s.copy_mode3_run_tokens_sum += num_tokens;
                for (size_t ti = 2; ti < copy_raw.size(); ti++) {
                    int run = (copy_raw[ti] & 0x3F) + 1;
                    s.copy_mode3_runs_sum += (uint64_t)run;
                    if (run >= 16) s.copy_mode3_long_runs++;
                }
            }
            payload_bits = (uint64_t)(copy_raw.size() - 2) * 8ull;
        }
        uint64_t stream_bits = (uint64_t)copy_wrapped.size() * 8ull;
        s.copy_stream_payload_bits_sum += payload_bits;
        s.copy_stream_overhead_bits_sum +=
            (stream_bits > payload_bits) ? (stream_bits - payload_bits) : 0ull;
    }

    s.tile4_stream_raw_bytes_sum += tile4_raw_size;
    if (tile4_raw_size > 0) {
        if (tile4_data.size() >= 2 && tile4_data[0] == FileHeader::WRAPPER_MAGIC_TILE4) {
            int tile4_mode = (int)tile4_data[1];
            if (tile4_mode == 1) s.tile4_stream_mode1++;
            else if (tile4_mode == 2) s.tile4_stream_mode2++;
            else s.tile4_stream_mode0++;
        } else {
            s.tile4_stream_mode0++;
        }
    }
}

} // namespace hakonyans::lossless_stream_diagnostics
