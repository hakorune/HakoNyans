#pragma once

#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "../platform/thread_budget.h"
#include "../platform/thread_pool.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <chrono>
#include <vector>

namespace hakonyans::lossless_filter_lo_codec {

inline ThreadPool& lo_codec_worker_pool() {
    static ThreadPool pool((int)std::max(1u, thread_budget::max_threads(8)));
    return pool;
}

// profile_code: 0=UI, 1=ANIME, 2=PHOTO
// Returns encoded filter_lo payload (raw or wrapped).
template <typename EncodeByteStreamFn, typename EncodeByteStreamSharedLzFn, typename CompressLzFn>
inline std::vector<uint8_t> encode_filter_lo_stream(
    const std::vector<uint8_t>& lo_bytes,
    const std::vector<uint8_t>& filter_ids,
    const std::vector<FileHeader::BlockType>& block_types,
    uint32_t pad_h,
    int nx,
    int profile_code,
    LosslessModeDebugStats* stats,
    EncodeByteStreamFn&& encode_byte_stream,
    EncodeByteStreamSharedLzFn&& encode_byte_stream_shared_lz,
    CompressLzFn&& compress_lz
) {
    ThreadPool& worker_pool = lo_codec_worker_pool();
    if (lo_bytes.empty()) return {};
    using Clock = std::chrono::steady_clock;
    auto ns_since = [](const Clock::time_point& t0, const Clock::time_point& t1) -> uint64_t {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };

    const unsigned int hw_threads = thread_budget::max_threads();
    thread_budget::ScopedThreadTokens base_parallel_tokens;
    if (hw_threads >= 4 && lo_bytes.size() >= 4096) {
        base_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(2);
    }
    const bool allow_parallel_base = base_parallel_tokens.acquired();

    std::vector<uint8_t> lo_legacy;
    std::vector<uint8_t> lo_lz;
    std::future<std::vector<uint8_t>> fut_legacy;
    std::future<std::vector<uint8_t>> fut_lz;

    Clock::time_point t_mode2_eval0 = Clock::now();
    if (allow_parallel_base) {
        fut_legacy = worker_pool.submit([&]() {
            thread_budget::ScopedParallelRegion guard;
            return encode_byte_stream(lo_bytes);
        });
        fut_lz = worker_pool.submit([&]() {
            thread_budget::ScopedParallelRegion guard;
            return compress_lz(lo_bytes);
        });
    } else {
        lo_legacy = encode_byte_stream(lo_bytes);
    }

    if (stats) stats->filter_lo_raw_bytes_sum += lo_bytes.size();

    constexpr int kFilterLoModeWrapperGainPermilleDefault = 990;
    constexpr int kFilterLoMode5GainPermille = 995;
    constexpr int kFilterLoMode5MinRawBytes = 2048;
    constexpr int kFilterLoMode5MinLZBytes = 1024;

    std::vector<uint8_t> delta_bytes(lo_bytes.size());
    delta_bytes[0] = lo_bytes[0];
    for (size_t i = 1; i < lo_bytes.size(); i++) {
        delta_bytes[i] = (uint8_t)(lo_bytes[i] - lo_bytes[i - 1]);
    }
    auto delta_rans = encode_byte_stream(delta_bytes);
    size_t delta_wrapped = 6 + delta_rans.size();

    if (allow_parallel_base) {
        lo_legacy = fut_legacy.get();
        lo_lz = fut_lz.get();
    } else {
        lo_lz = compress_lz(lo_bytes);
    }
    if (stats) {
        stats->filter_lo_mode2_eval_ns += ns_since(t_mode2_eval0, Clock::now());
    }

    size_t legacy_size = lo_legacy.size();
    size_t lz_wrapped = 6 + lo_lz.size();

    std::vector<uint8_t> lo_lz_rans;
    size_t lz_rans_wrapped = std::numeric_limits<size_t>::max();
    if (lo_bytes.size() >= kFilterLoMode5MinRawBytes && lo_lz.size() >= kFilterLoMode5MinLZBytes) {
        if (stats) stats->filter_lo_mode5_candidates++;
        const auto t_mode5_eval0 = Clock::now();
        lo_lz_rans = encode_byte_stream_shared_lz(lo_lz);
        if (stats) stats->filter_lo_mode5_eval_ns += ns_since(t_mode5_eval0, Clock::now());
        lz_rans_wrapped = 6 + lo_lz_rans.size();
        if (stats) {
            stats->filter_lo_mode5_candidate_bytes_sum += lo_lz.size();
            stats->filter_lo_mode5_wrapped_bytes_sum += lz_rans_wrapped;
            stats->filter_lo_mode5_legacy_bytes_sum += legacy_size;
        }
    }

    int best_mode = 0;
    size_t best_size = legacy_size;

    if (delta_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
        if (delta_wrapped < best_size) {
            best_size = delta_wrapped;
            best_mode = 1;
        }
    }

    if (lz_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
        if (stats) stats->filter_lo_mode2_candidate_bytes_sum += lz_wrapped;
        if (lz_wrapped < best_size) {
            best_size = lz_wrapped;
            best_mode = 2;
        }
    } else {
        if (stats) stats->filter_lo_mode2_reject_gate++;
    }

    if (lz_rans_wrapped != std::numeric_limits<size_t>::max()) {
        bool better_than_legacy = (lz_rans_wrapped * 1000 <= legacy_size * kFilterLoMode5GainPermille);
        bool better_than_lz = (lz_rans_wrapped * 100 <= lz_wrapped * 99);

        if (better_than_legacy && better_than_lz) {
            if (lz_rans_wrapped < best_size) {
                best_size = lz_rans_wrapped;
                best_mode = 5;
            } else {
                if (stats) stats->filter_lo_mode5_reject_best++;
            }
        } else {
            if (stats) stats->filter_lo_mode5_reject_gate++;
        }
    }

    std::vector<uint8_t> pred_stream;
    std::vector<uint8_t> resid_stream;
    std::vector<uint8_t> mode3_preds;
    std::vector<int> row_lens;

    std::vector<std::vector<uint8_t>> mode4_streams(6);
    std::vector<uint32_t> mode4_ctx_raw_counts(6, 0);

    const bool enable_mode3_mode4 = ((profile_code == 1 || profile_code == 2) && lo_bytes.size() > 256);
    if (enable_mode3_mode4) {
        const auto t_mode3_eval0 = Clock::now();
        std::vector<int> dct_row_lens(std::max(1u, pad_h / 8u), 0);
        for (uint32_t by = 0; by < pad_h / 8u; by++) {
            int dct_cols = 0;
            const size_t row_off = (size_t)by * (size_t)nx;
            for (int bx = 0; bx < nx; bx++) {
                if (block_types[row_off + (size_t)bx] == FileHeader::BlockType::DCT) dct_cols++;
            }
            dct_row_lens[(size_t)by] = dct_cols * 8;
        }
        row_lens.assign(pad_h, 0);
        for (uint32_t y = 0; y < pad_h; y++) {
            row_lens[y] = dct_row_lens[(size_t)(y / 8)];
        }

        std::vector<uint8_t> preds;
        std::vector<uint8_t> resids;
        size_t active_rows = 0;
        for (int len : row_lens) if (len > 0) active_rows++;
        preds.reserve(active_rows);
        resids.resize(lo_bytes.size());

        size_t offset = 0;
        size_t prev_valid_row_start = 0;
        size_t prev_valid_row_len = 0;
        size_t resid_write = 0;

        for (uint32_t y = 0; y < pad_h; y++) {
            int len = row_lens[y];
            if (len == 0) continue;

            const uint8_t* curr_row = &lo_bytes[offset];
            int best_p = 0;
            int64_t min_cost = -1;

            for (int p = 0; p < 4; p++) {
                int64_t cost = 0;
                for (int i = 0; i < len; i++) {
                    uint8_t pred_val = 0;
                    if (p == 1) {
                        pred_val = (i == 0) ? 0 : curr_row[i - 1];
                    } else if (p == 2) {
                        pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                    } else if (p == 3) {
                        uint8_t left = (i == 0) ? 0 : curr_row[i - 1];
                        uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                        pred_val = (left + up) / 2;
                    }
                    int diff = (int)curr_row[i] - pred_val;
                    if (diff < 0) diff += 256;
                    if (diff > 128) diff = 256 - diff;
                    cost += diff;
                    if (min_cost != -1 && cost >= min_cost) break;
                }
                if (min_cost == -1 || cost < min_cost) {
                    min_cost = cost;
                    best_p = p;
                }
            }

            preds.push_back((uint8_t)best_p);

            for (int i = 0; i < len; i++) {
                uint8_t pred_val = 0;
                if (best_p == 1) pred_val = (i == 0) ? 0 : curr_row[i - 1];
                else if (best_p == 2) pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                else if (best_p == 3) {
                    uint8_t left = (i == 0) ? 0 : curr_row[i - 1];
                    uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                    pred_val = (left + up) / 2;
                }
                resids[resid_write++] = (uint8_t)(curr_row[i] - pred_val);
            }

            prev_valid_row_start = offset;
            prev_valid_row_len = len;
            offset += len;
        }
        resids.resize(resid_write);

        auto preds_enc = encode_byte_stream(preds);
        auto resids_enc = encode_byte_stream(resids);
        size_t total_sz = 1 + 1 + 4 + 4 + preds_enc.size() + resids_enc.size();
        if (stats) stats->filter_lo_mode3_eval_ns += ns_since(t_mode3_eval0, Clock::now());

        if (total_sz < best_size && total_sz * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
            best_size = total_sz;
            best_mode = 3;
            pred_stream = std::move(preds_enc);
            resid_stream = std::move(resids_enc);
            mode3_preds = std::move(preds);
        }

        const auto t_mode4_eval0 = Clock::now();
        std::vector<std::vector<uint8_t>> lo_ctx(6);
        std::array<uint32_t, 6> ctx_reserved = {0, 0, 0, 0, 0, 0};
        for (uint32_t y = 0; y < pad_h; y++) {
            int len = row_lens[y];
            if (len <= 0) continue;
            uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
            if (fid > 5) fid = 0;
            ctx_reserved[fid] += (uint32_t)len;
        }
        for (int k = 0; k < 6; k++) lo_ctx[k].reserve((size_t)ctx_reserved[k]);

        size_t off = 0;
        for (uint32_t y = 0; y < pad_h; y++) {
            int len = row_lens[y];
            if (len <= 0) continue;
            size_t end_off = std::min(off + (size_t)len, lo_bytes.size());
            if (end_off <= off) break;
            uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
            if (fid > 5) fid = 0;
            size_t take = end_off - off;
            auto& dst = lo_ctx[fid];
            size_t dst_off = dst.size();
            dst.resize(dst_off + take);
            std::memcpy(dst.data() + dst_off, lo_bytes.data() + off, take);
            off = end_off;
        }

        size_t mode4_sz = 1 + 1 + 4 + 6 * 4;
        const size_t mode4_gate_limit =
            (legacy_size * (size_t)kFilterLoModeWrapperGainPermilleDefault + 999u) / 1000u;
        int nonempty_ctx = 0;
        std::vector<std::vector<uint8_t>> ctx_streams(6);
        std::vector<uint32_t> ctx_raw_counts(6, 0);
        bool mode4_aborted = false;
        thread_budget::ScopedThreadTokens ctx_parallel_tokens;
        if (hw_threads >= 6 && lo_bytes.size() >= 8192) {
            ctx_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(6);
        }
        const bool allow_parallel_ctx = ctx_parallel_tokens.acquired();
        if (allow_parallel_ctx) {
            std::vector<std::future<std::vector<uint8_t>>> futs(6);
            std::vector<bool> launched(6, false);
            for (int k = 0; k < 6; k++) {
                ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
                if (lo_ctx[k].empty()) continue;
                nonempty_ctx++;
                launched[k] = true;
                futs[k] = worker_pool.submit([&, k]() {
                    thread_budget::ScopedParallelRegion guard;
                    return encode_byte_stream(lo_ctx[k]);
                });
            }
            for (int k = 0; k < 6; k++) {
                if (launched[k]) {
                    ctx_streams[k] = futs[k].get();
                }
                mode4_sz += ctx_streams[k].size();
            }
        } else {
            for (int k = 0; k < 6; k++) {
                ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
                if (!lo_ctx[k].empty()) nonempty_ctx++;
                if (!lo_ctx[k].empty()) {
                    ctx_streams[k] = encode_byte_stream(lo_ctx[k]);
                }
                mode4_sz += ctx_streams[k].size();
                if (mode4_sz > mode4_gate_limit || mode4_sz >= best_size) {
                    mode4_aborted = true;
                    break;
                }
            }
        }

        if (!mode4_aborted &&
            nonempty_ctx >= 2 &&
            mode4_sz * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
            if (stats) stats->filter_lo_mode4_candidate_bytes_sum += mode4_sz;
            if (mode4_sz < best_size) {
                best_size = mode4_sz;
                best_mode = 4;
                mode4_streams = std::move(ctx_streams);
                mode4_ctx_raw_counts = std::move(ctx_raw_counts);
            }
        } else if (nonempty_ctx >= 2) {
            if (stats) stats->filter_lo_mode4_reject_gate++;
        }
        if (stats) stats->filter_lo_mode4_eval_ns += ns_since(t_mode4_eval0, Clock::now());
    }

    std::vector<uint8_t> lo_stream;
    if (best_mode == 0) {
        lo_stream = std::move(lo_legacy);
        if (stats) stats->filter_lo_mode0++;
    } else if (best_mode == 3) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(3);

        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));

        uint32_t ps = (uint32_t)pred_stream.size();
        lo_stream.push_back((uint8_t)(ps & 0xFF));
        lo_stream.push_back((uint8_t)((ps >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((ps >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((ps >> 24) & 0xFF));

        lo_stream.insert(lo_stream.end(), pred_stream.begin(), pred_stream.end());
        lo_stream.insert(lo_stream.end(), resid_stream.begin(), resid_stream.end());

        if (stats) {
            stats->filter_lo_mode3++;
            stats->filter_lo_mode3_rows_sum += mode3_preds.size();
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode3_saved_bytes_sum += (lo_legacy.size() - lo_stream.size());
            }
            for (uint8_t p : mode3_preds) {
                if (p < 4) stats->filter_lo_mode3_pred_hist[p]++;
            }
        }
    } else if (best_mode == 4) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(4);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
        for (int k = 0; k < 6; k++) {
            uint32_t len = (uint32_t)mode4_streams[k].size();
            lo_stream.push_back((uint8_t)(len & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
        }
        for (int k = 0; k < 6; k++) {
            lo_stream.insert(lo_stream.end(), mode4_streams[k].begin(), mode4_streams[k].end());
        }
        if (stats) {
            stats->filter_lo_mode4++;
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode4_saved_bytes_sum +=
                    (uint64_t)(lo_legacy.size() - lo_stream.size());
            }
            int nonempty_ctx = 0;
            for (int k = 0; k < 6; k++) {
                stats->filter_lo_ctx_bytes_sum[k] += mode4_ctx_raw_counts[k];
                if (mode4_ctx_raw_counts[k] > 0) nonempty_ctx++;
            }
            if (nonempty_ctx > 0) {
                stats->filter_lo_ctx_nonempty_tiles++;
            }
        }
    } else {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back((uint8_t)best_mode);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));

        if (best_mode == 1) {
            lo_stream.insert(lo_stream.end(), delta_rans.begin(), delta_rans.end());
            if (stats) stats->filter_lo_mode1++;
        } else if (best_mode == 2) {
            lo_stream.insert(lo_stream.end(), lo_lz.begin(), lo_lz.end());
            if (stats) stats->filter_lo_mode2++;
        } else {
            lo_stream.insert(lo_stream.end(), lo_lz_rans.begin(), lo_lz_rans.end());
            if (stats) {
                stats->filter_lo_mode5++;
                if (lo_legacy.size() > lo_stream.size()) {
                    stats->filter_lo_mode5_saved_bytes_sum +=
                        (uint64_t)(lo_legacy.size() - lo_stream.size());
                }
            }
        }
    }

    if (stats) stats->filter_lo_compressed_bytes_sum += lo_stream.size();
    return lo_stream;
}

} // namespace hakonyans::lossless_filter_lo_codec
