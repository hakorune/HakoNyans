#pragma once

#include "byte_stream_encoder.h"
#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include <vector>

namespace hakonyans::filter_hi_wrapper {

/**
 * Encode filter_hi stream with sparse-or-dense selection.
 *
 * Dense:
 *   - raw rANS stream of hi_bytes (no wrapper)
 *
 * Sparse:
 *   - [WRAPPER_MAGIC_FILTER_HI][nz_lo][nz_mid][nz_hi][zero_mask...][nonzero_rANS...]
 *   - This matches decode.h sparse path exactly.
 *
 * Selection rule:
 *   - consider sparse only when zero_ratio >= 0.75 and size >= 32
 *   - choose smaller of sparse vs dense
 */
inline std::vector<uint8_t> encode_filter_hi_stream(
    const std::vector<uint8_t>& hi_bytes,
    LosslessModeDebugStats* debug_stats = nullptr
) {
    if (hi_bytes.empty()) return {};

    size_t zero_count = 0;
    for (uint8_t b : hi_bytes) {
        if (b == 0) zero_count++;
    }
    double zero_ratio = (double)zero_count / (double)hi_bytes.size();

    if (debug_stats) {
        debug_stats->filter_hi_raw_bytes_sum += hi_bytes.size();
        debug_stats->filter_hi_zero_ratio_sum += (uint64_t)(zero_ratio * 100.0);
    }

    // Dense baseline: standard rANS (backward compatible, no wrapper marker)
    auto dense_stream = byte_stream_encoder::encode_byte_stream(hi_bytes);

    // Sparse candidate
    if (zero_ratio >= 0.75 && hi_bytes.size() >= 32) {
        size_t mask_size = (hi_bytes.size() + 7) / 8;
        std::vector<uint8_t> zero_mask(mask_size, 0);
        std::vector<uint8_t> nonzero_vals;
        nonzero_vals.reserve(hi_bytes.size() - zero_count);

        for (size_t i = 0; i < hi_bytes.size(); i++) {
            if (hi_bytes[i] != 0) {
                zero_mask[i / 8] |= (uint8_t)(1u << (i % 8));
                nonzero_vals.push_back(hi_bytes[i]);
            }
        }

        std::vector<uint8_t> sparse_stream;
        sparse_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_HI);
        uint32_t nz_count = (uint32_t)nonzero_vals.size();
        sparse_stream.push_back((uint8_t)(nz_count & 0xFF));
        sparse_stream.push_back((uint8_t)((nz_count >> 8) & 0xFF));
        sparse_stream.push_back((uint8_t)((nz_count >> 16) & 0xFF));
        sparse_stream.insert(sparse_stream.end(), zero_mask.begin(), zero_mask.end());

        if (!nonzero_vals.empty()) {
            auto nz_rans = byte_stream_encoder::encode_byte_stream(nonzero_vals);
            sparse_stream.insert(sparse_stream.end(), nz_rans.begin(), nz_rans.end());
        }

        if (sparse_stream.size() < dense_stream.size()) {
            if (debug_stats) {
                debug_stats->filter_hi_sparse_count++;
                debug_stats->filter_hi_compressed_bytes_sum += sparse_stream.size();
            }
            return sparse_stream;
        }
    }

    if (debug_stats) {
        debug_stats->filter_hi_dense_count++;
        debug_stats->filter_hi_compressed_bytes_sum += dense_stream.size();
    }
    return dense_stream;
}

} // namespace hakonyans::filter_hi_wrapper
