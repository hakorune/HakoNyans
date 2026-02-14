#pragma once

#include "headers.h"
#include "lossless_decode_debug_stats.h"
#include "../platform/thread_budget.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <vector>

namespace hakonyans::lossless_filter_lo_decode {

template <typename DecodeByteStreamFn, typename DecodeByteStreamSharedLzFn, typename DecompressLzFn>
inline std::vector<uint8_t> decode_filter_lo_stream(
    const uint8_t* ptr_lo,
    uint32_t lo_stream_size,
    uint32_t filter_pixel_count,
    const std::vector<uint8_t>& filter_ids,
    const std::vector<FileHeader::BlockType>& block_types,
    uint32_t pad_h,
    int nx,
    bool use_shared_lz_cdf,
    bool allow_mode6,
    uint16_t file_version,  // For Mode 6 backward compat (0x0015 vs 0x0016)
    DecodeByteStreamFn&& decode_byte_stream,
    DecodeByteStreamSharedLzFn&& decode_byte_stream_shared_lz,
    DecompressLzFn&& decompress_lz,
    ::hakonyans::LosslessDecodeDebugStats* stats = nullptr
) {
    using Clock = std::chrono::steady_clock;
    auto add_ns = [&](uint64_t* dst, const Clock::time_point& t0, const Clock::time_point& t1) {
        if (!dst) return;
        *dst += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };
    auto timed_decode_rans = [&](const uint8_t* data, size_t size, size_t raw_count) {
        const auto t0 = Clock::now();
        auto out = decode_byte_stream(data, size, raw_count);
        const auto t1 = Clock::now();
        add_ns(stats ? &stats->plane_filter_lo_decode_rans_ns : nullptr, t0, t1);
        return out;
    };
    auto timed_decode_shared_rans = [&](const uint8_t* data, size_t size, size_t raw_count) {
        const auto t0 = Clock::now();
        auto out = decode_byte_stream_shared_lz(data, size, raw_count);
        const auto t1 = Clock::now();
        add_ns(stats ? &stats->plane_filter_lo_decode_shared_rans_ns : nullptr, t0, t1);
        return out;
    };
    auto timed_decompress_lz = [&](const uint8_t* data, size_t size, size_t raw_count) {
        const auto t0 = Clock::now();
        auto out = decompress_lz(data, size, raw_count);
        const auto t1 = Clock::now();
        add_ns(stats ? &stats->plane_filter_lo_tilelz_decompress_ns : nullptr, t0, t1);
        return out;
    };

    std::vector<uint8_t> lo_bytes;
    if (lo_stream_size == 0 || filter_pixel_count == 0) return lo_bytes;

    if (lo_stream_size >= 6 && ptr_lo[0] == FileHeader::WRAPPER_MAGIC_FILTER_LO) {
        uint8_t lo_mode = ptr_lo[1];
        uint32_t raw_count = (uint32_t)ptr_lo[2]
                           | ((uint32_t)ptr_lo[3] << 8)
                           | ((uint32_t)ptr_lo[4] << 16)
                           | ((uint32_t)ptr_lo[5] << 24);
        const uint8_t* payload = ptr_lo + 6;
        size_t payload_size = lo_stream_size - 6;

        if (lo_mode == 1) {
            if (stats) stats->plane_filter_lo_mode1_count++;
            auto delta = timed_decode_rans(payload, payload_size, raw_count);
            lo_bytes.resize(raw_count);
            if (!delta.empty()) {
                lo_bytes[0] = delta[0];
                for (size_t i = 1; i < raw_count && i < delta.size(); i++) {
                    lo_bytes[i] = (uint8_t)(lo_bytes[i - 1] + delta[i]);
                }
            }
        } else if (lo_mode == 2) {
            if (stats) stats->plane_filter_lo_mode2_count++;
            lo_bytes = timed_decompress_lz(payload, payload_size, raw_count);
        } else if (lo_mode == 5) {
            if (stats) stats->plane_filter_lo_mode5_count++;
            std::vector<uint8_t> lz_payload;
            if (use_shared_lz_cdf) {
                if (stats) stats->plane_filter_lo_mode5_shared_cdf_count++;
                lz_payload = timed_decode_shared_rans(payload, payload_size, 0);
            } else {
                if (stats) stats->plane_filter_lo_mode5_legacy_cdf_count++;
                lz_payload = timed_decode_rans(payload, payload_size, 0);
            }
            if (!lz_payload.empty()) {
                lo_bytes = timed_decompress_lz(lz_payload.data(), lz_payload.size(), raw_count);
            } else {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            }
        } else if (lo_mode == 6 && allow_mode6) {
            #include "lossless_filter_lo_decode_mode6.inc"
        } else if (lo_mode == 3 && payload_size >= 4) {
            if (stats) stats->plane_filter_lo_mode3_count++;
            uint32_t pred_sz = (uint32_t)payload[0]
                             | ((uint32_t)payload[1] << 8)
                             | ((uint32_t)payload[2] << 16)
                             | ((uint32_t)payload[3] << 24);
            if ((size_t)pred_sz + 4 > payload_size) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                const uint8_t* pred_ptr = payload + 4;
                const uint8_t* resid_ptr = payload + 4 + pred_sz;
                size_t resid_sz = payload_size - 4 - pred_sz;

                const auto t_mode3_rows0 = Clock::now();
                std::vector<int> row_lens(pad_h, 0);
                int active_rows = 0;
                for (uint32_t y = 0; y < pad_h; y++) {
                    int count = 0;
                    int row_idx = y / 8;
                    for (int bx = 0; bx < nx; bx++) {
                        if (block_types[row_idx * nx + bx] == FileHeader::BlockType::DCT) {
                            count += 8;
                        }
                    }
                    row_lens[y] = count;
                    if (count > 0) active_rows++;
                }
                const auto t_mode3_rows1 = Clock::now();
                add_ns(stats ? &stats->plane_filter_lo_mode3_row_lens_ns : nullptr, t_mode3_rows0, t_mode3_rows1);
                if (stats) stats->plane_filter_lo_mode3_active_rows_sum += (uint64_t)active_rows;

                auto preds = timed_decode_rans(pred_ptr, pred_sz, active_rows);
                auto resids = timed_decode_rans(resid_ptr, resid_sz, raw_count);

                lo_bytes.assign(raw_count, 0);
                uint8_t* lo_ptr = lo_bytes.data();
                size_t resid_idx = 0;
                size_t pred_idx = 0;
                size_t out_idx = 0;
                size_t prev_valid_row_start = 0;
                size_t prev_valid_row_len = 0;

                for (uint32_t y = 0; y < pad_h; y++) {
                    int len = row_lens[y];
                    if (len == 0) continue;

                    int p = (pred_idx < preds.size()) ? preds[pred_idx++] : 0;
                    size_t start_idx = out_idx;
                    if (start_idx >= raw_count) break;
                    size_t safe_len = std::min<size_t>((size_t)len, raw_count - start_idx);
                    if (safe_len == 0) continue;

                    if (p == 1) {
                        uint8_t left = 0;
                        for (size_t i = 0; i < safe_len; i++) {
                            uint8_t resid = (resid_idx < resids.size()) ? resids[resid_idx++] : 0;
                            uint8_t v = (uint8_t)(resid + left);
                            lo_ptr[start_idx + i] = v;
                            left = v;
                        }
                    } else if (p == 2) {
                        for (size_t i = 0; i < safe_len; i++) {
                            uint8_t resid = (resid_idx < resids.size()) ? resids[resid_idx++] : 0;
                            uint8_t up = (prev_valid_row_len > i) ? lo_ptr[prev_valid_row_start + i] : 0;
                            lo_ptr[start_idx + i] = (uint8_t)(resid + up);
                        }
                    } else if (p == 3) {
                        uint8_t left = 0;
                        for (size_t i = 0; i < safe_len; i++) {
                            uint8_t resid = (resid_idx < resids.size()) ? resids[resid_idx++] : 0;
                            uint8_t up = (prev_valid_row_len > i) ? lo_ptr[prev_valid_row_start + i] : 0;
                            uint8_t pred_val = (uint8_t)((left + up) / 2u);
                            uint8_t v = (uint8_t)(resid + pred_val);
                            lo_ptr[start_idx + i] = v;
                            left = v;
                        }
                    } else {
                        for (size_t i = 0; i < safe_len; i++) {
                            uint8_t resid = (resid_idx < resids.size()) ? resids[resid_idx++] : 0;
                            lo_ptr[start_idx + i] = resid;
                        }
                    }
                    out_idx += safe_len;
                    prev_valid_row_start = start_idx;
                    prev_valid_row_len = safe_len;
                }
            }
        } else if (lo_mode == 4 && payload_size >= 24) {
            if (stats) stats->plane_filter_lo_mode4_count++;
            uint32_t lens[6] = {0, 0, 0, 0, 0, 0};
            for (int k = 0; k < 6; k++) {
                const size_t pos = (size_t)k * 4;
                lens[k] = (uint32_t)payload[pos]
                        | ((uint32_t)payload[pos + 1] << 8)
                        | ((uint32_t)payload[pos + 2] << 16)
                        | ((uint32_t)payload[pos + 3] << 24);
            }
            size_t off = 24;
            bool lens_ok = true;
            for (int k = 0; k < 6; k++) {
                if (lens[k] > payload_size - off) {
                    lens_ok = false;
                    break;
                }
                off += lens[k];
            }

            if (!lens_ok) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                const auto t_mode4_rows0 = Clock::now();
                std::vector<int> row_lens(pad_h, 0);
                std::vector<uint32_t> ctx_expected(6, 0);
                for (uint32_t y = 0; y < pad_h; y++) {
                    int count = 0;
                    int row_idx = y / 8;
                    for (int bx = 0; bx < nx; bx++) {
                        if (block_types[row_idx * nx + bx] == FileHeader::BlockType::DCT) {
                            count += 8;
                        }
                    }
                    row_lens[y] = count;
                    if (count > 0) {
                        uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
                        if (fid > 5) fid = 0;
                        ctx_expected[fid] += (uint32_t)count;
                    }
                }
                const auto t_mode4_rows1 = Clock::now();
                add_ns(stats ? &stats->plane_filter_lo_mode4_row_lens_ns : nullptr, t_mode4_rows0, t_mode4_rows1);
                if (stats) {
                    uint64_t nonempty_ctx = 0;
                    for (int k = 0; k < 6; k++) {
                        if (ctx_expected[k] > 0) nonempty_ctx++;
                    }
                    stats->plane_filter_lo_mode4_nonempty_ctx_sum += nonempty_ctx;
                }

                std::vector<std::vector<uint8_t>> ctx_decoded(6);
                size_t ctx_offsets[6] = {0, 0, 0, 0, 0, 0};
                off = 24;
                for (int k = 0; k < 6; k++) {
                    ctx_offsets[k] = off;
                    off += lens[k];
                }

                const unsigned int hw_threads = thread_budget::max_threads();
                thread_budget::ScopedThreadTokens ctx_parallel_tokens;
                if (hw_threads >= 6 && raw_count >= 8192) {
                    ctx_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(6);
                }
                const bool allow_parallel_ctx = ctx_parallel_tokens.acquired();
                if (stats) {
                    if (allow_parallel_ctx) stats->plane_filter_lo_mode4_parallel_ctx_tiles++;
                    else stats->plane_filter_lo_mode4_sequential_ctx_tiles++;
                }
                if (allow_parallel_ctx) {
                    struct CtxDecodeResult {
                        std::vector<uint8_t> bytes;
                        uint64_t elapsed_ns = 0;
                    };
                    std::vector<std::future<CtxDecodeResult>> futs(6);
                    std::vector<bool> launched(6, false);
                    for (int k = 0; k < 6; k++) {
                        if (lens[k] == 0) continue;
                        launched[k] = true;
                        futs[k] = std::async(std::launch::async, [&, k]() {
                            thread_budget::ScopedParallelRegion guard;
                            const auto t_ctx0 = Clock::now();
                            auto out = decode_byte_stream(
                                payload + ctx_offsets[k], lens[k], ctx_expected[k]
                            );
                            const auto t_ctx1 = Clock::now();
                            CtxDecodeResult r;
                            r.bytes = std::move(out);
                            r.elapsed_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                t_ctx1 - t_ctx0
                            ).count();
                            return r;
                        });
                    }
                    for (int k = 0; k < 6; k++) {
                        if (!launched[k]) continue;
                        auto r = futs[k].get();
                        ctx_decoded[k] = std::move(r.bytes);
                        if (stats) stats->plane_filter_lo_decode_rans_ns += r.elapsed_ns;
                    }
                } else {
                    for (int k = 0; k < 6; k++) {
                        if (lens[k] > 0) {
                            ctx_decoded[k] = timed_decode_rans(
                                payload + ctx_offsets[k], lens[k], ctx_expected[k]
                            );
                        }
                    }
                }

                std::vector<size_t> ctx_pos(6, 0);
                lo_bytes.assign(raw_count, 0);
                uint8_t* lo_ptr = lo_bytes.data();
                size_t out_idx = 0;
                for (uint32_t y = 0; y < pad_h && out_idx < raw_count; y++) {
                    int len = row_lens[y];
                    if (len <= 0) continue;
                    uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
                    if (fid > 5) fid = 0;
                    size_t take = std::min<size_t>((size_t)len, raw_count - out_idx);
                    const auto& ctx = ctx_decoded[fid];
                    size_t pos = ctx_pos[fid];
                    size_t available = (pos < ctx.size()) ? (ctx.size() - pos) : 0;
                    size_t copy_n = std::min(take, available);
                    if (copy_n > 0) {
                        std::memcpy(lo_ptr + out_idx, ctx.data() + pos, copy_n);
                        ctx_pos[fid] += copy_n;
                    }
                    out_idx += take;
                }
            }
        } else if (lo_mode == 7 && payload_size >= 28) {
            #include "lossless_filter_lo_decode_mode7.inc"
        } else if (lo_mode == 8 && file_version >= FileHeader::VERSION_FILTER_LO_CTX_HYBRID_CODEC && payload_size >= 30) {
            #include "lossless_filter_lo_decode_mode8.inc"
        } else {
            if (stats) {
                stats->plane_filter_lo_mode_invalid_count++;
                stats->plane_filter_lo_fallback_zero_fill_count++;
            }
            lo_bytes.assign(raw_count, 0);
        }

        if (lo_bytes.size() < filter_pixel_count) {
            if (stats) stats->plane_filter_lo_zero_pad_bytes_sum +=
                (uint64_t)(filter_pixel_count - lo_bytes.size());
            lo_bytes.resize(filter_pixel_count, 0);
        }
    } else {
        if (stats) stats->plane_filter_lo_mode_raw_count++;
        lo_bytes = timed_decode_rans(ptr_lo, lo_stream_size, filter_pixel_count);
    }

    return lo_bytes;
}

} // namespace hakonyans::lossless_filter_lo_decode
