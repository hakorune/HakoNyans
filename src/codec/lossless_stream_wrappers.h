#pragma once

#include "copy.h"
#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_stream_wrappers {

inline int detect_wrapper_mode(const std::vector<uint8_t>& stream, uint8_t magic) {
    if (!stream.empty() && stream[0] == magic && stream.size() >= 2) {
        return (int)stream[1];
    }
    return 0;
}

template <typename EncodeByteStreamFn, typename CompressLzFn>
inline std::vector<uint8_t> wrap_palette_stream(
    const std::vector<uint8_t>& pal_raw,
    EncodeByteStreamFn&& encode_byte_stream,
    CompressLzFn&& compress_lz,
    LosslessModeDebugStats* stats
) {
    std::vector<uint8_t> pal_data = pal_raw;
    if (pal_data.empty()) return pal_data;

    const size_t raw_size = pal_data.size();

    auto encoded_pal = encode_byte_stream(pal_data);
    if (!encoded_pal.empty()) {
        std::vector<uint8_t> compact_pal;
        compact_pal.reserve(1 + 1 + 4 + encoded_pal.size());
        compact_pal.push_back(FileHeader::WRAPPER_MAGIC_PALETTE);
        compact_pal.push_back(1);
        uint32_t raw_count = (uint32_t)pal_data.size();
        compact_pal.resize(compact_pal.size() + 4);
        std::memcpy(compact_pal.data() + 2, &raw_count, 4);
        compact_pal.insert(compact_pal.end(), encoded_pal.begin(), encoded_pal.end());
        if (compact_pal.size() < pal_data.size()) {
            if (stats) {
                stats->palette_stream_compact_count++;
                stats->palette_stream_compact_saved_bytes_sum +=
                    (uint64_t)(pal_data.size() - compact_pal.size());
            }
            pal_data = std::move(compact_pal);
        }
    }

    auto lz_pal = compress_lz(pal_raw);
    if (!lz_pal.empty()) {
        std::vector<uint8_t> lz_wrapped;
        lz_wrapped.reserve(1 + 1 + 4 + lz_pal.size());
        lz_wrapped.push_back(FileHeader::WRAPPER_MAGIC_PALETTE);
        lz_wrapped.push_back(2);
        uint32_t raw_count = (uint32_t)pal_raw.size();
        lz_wrapped.resize(lz_wrapped.size() + 4);
        std::memcpy(lz_wrapped.data() + 2, &raw_count, 4);
        lz_wrapped.insert(lz_wrapped.end(), lz_pal.begin(), lz_pal.end());
        if (lz_wrapped.size() < pal_data.size()) {
            if (stats) {
                stats->palette_lz_used_count++;
                stats->palette_lz_saved_bytes_sum +=
                    (uint64_t)(raw_size - lz_wrapped.size());
            }
            pal_data = std::move(lz_wrapped);
        }
    }

    return pal_data;
}

struct CopyWrapResult {
    std::vector<uint8_t> raw;
    std::vector<uint8_t> wrapped;
    int mode = 0; // 0=raw, 1=rANS wrapper, 2=LZ wrapper
};

template <typename EncodeByteStreamFn, typename CompressLzFn>
inline CopyWrapResult wrap_copy_stream(
    const std::vector<CopyParams>& copy_ops,
    EncodeByteStreamFn&& encode_byte_stream,
    CompressLzFn&& compress_lz,
    LosslessModeDebugStats* stats
) {
    CopyWrapResult out;
    out.raw = CopyCodec::encode_copy_stream(copy_ops);
    out.wrapped = out.raw;
    out.mode = 0;

    if (out.raw.empty()) return out;

    auto cpy_rans = encode_byte_stream(out.raw);
    if (!cpy_rans.empty()) {
        std::vector<uint8_t> wrapped;
        wrapped.reserve(1 + 1 + 4 + cpy_rans.size());
        wrapped.push_back(FileHeader::WRAPPER_MAGIC_COPY);
        wrapped.push_back(1);
        uint32_t raw_count = (uint32_t)out.raw.size();
        wrapped.resize(wrapped.size() + 4);
        std::memcpy(wrapped.data() + 2, &raw_count, 4);
        wrapped.insert(wrapped.end(), cpy_rans.begin(), cpy_rans.end());
        if (wrapped.size() < out.wrapped.size()) {
            out.wrapped = std::move(wrapped);
            out.mode = 1;
        }
    }

    auto cpy_lz = compress_lz(out.raw);
    if (!cpy_lz.empty()) {
        std::vector<uint8_t> wrapped;
        wrapped.reserve(1 + 1 + 4 + cpy_lz.size());
        wrapped.push_back(FileHeader::WRAPPER_MAGIC_COPY);
        wrapped.push_back(2);
        uint32_t raw_count = (uint32_t)out.raw.size();
        wrapped.resize(wrapped.size() + 4);
        std::memcpy(wrapped.data() + 2, &raw_count, 4);
        wrapped.insert(wrapped.end(), cpy_lz.begin(), cpy_lz.end());
        if (wrapped.size() < out.wrapped.size()) {
            out.wrapped = std::move(wrapped);
            out.mode = 2;
        }
    }

    if (out.mode == 2 && stats) {
        stats->copy_lz_used_count++;
        stats->copy_lz_saved_bytes_sum +=
            (uint64_t)(out.raw.size() - out.wrapped.size());
    }

    return out;
}

template <typename EncodeByteStreamFn, typename CompressLzFn>
inline std::vector<uint8_t> wrap_filter_ids_stream(
    const std::vector<uint8_t>& filter_ids,
    EncodeByteStreamFn&& encode_byte_stream,
    CompressLzFn&& compress_lz,
    LosslessModeDebugStats* stats
) {
    if (stats) stats->filter_ids_raw_bytes_sum += filter_ids.size();

    std::vector<uint8_t> filter_ids_packed;

    if (filter_ids.size() >= 8) {
        auto fid_rans = encode_byte_stream(filter_ids);
        size_t rans_wrapped_size = 2 + fid_rans.size();

        auto fid_lz = compress_lz(filter_ids);
        size_t lz_wrapped_size = 2 + fid_lz.size();

        size_t raw_size = filter_ids.size();
        size_t best_size = raw_size;
        int best_mode = 0;

        if (rans_wrapped_size < best_size) {
            best_size = rans_wrapped_size;
            best_mode = 1;
        }
        if (lz_wrapped_size < best_size) {
            best_size = lz_wrapped_size;
            best_mode = 2;
        }

        if (best_mode == 1) {
            filter_ids_packed.push_back(FileHeader::WRAPPER_MAGIC_FILTER_IDS);
            filter_ids_packed.push_back(1);
            filter_ids_packed.insert(filter_ids_packed.end(), fid_rans.begin(), fid_rans.end());
            if (stats) stats->filter_ids_mode1++;
        } else if (best_mode == 2) {
            filter_ids_packed.push_back(FileHeader::WRAPPER_MAGIC_FILTER_IDS);
            filter_ids_packed.push_back(2);
            filter_ids_packed.insert(filter_ids_packed.end(), fid_lz.begin(), fid_lz.end());
            if (stats) stats->filter_ids_mode2++;
        } else {
            filter_ids_packed = filter_ids;
            if (stats) stats->filter_ids_mode0++;
        }
    } else {
        filter_ids_packed = filter_ids;
        if (stats) stats->filter_ids_mode0++;
    }

    if (stats) stats->filter_ids_compressed_bytes_sum += filter_ids_packed.size();
    return filter_ids_packed;
}

} // namespace hakonyans::lossless_stream_wrappers
