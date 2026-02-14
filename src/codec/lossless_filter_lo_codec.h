#pragma once

#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_filter_lo_codec_utils.h"
#include "../platform/thread_budget.h"
#include "../platform/thread_pool.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
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
    CompressLzFn&& compress_lz,
    bool enable_lz_probe = false
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

    // DOC: docs/LOSSLESS_FLOW_MAP.md#filter-lo-lz-probe
    bool evaluate_lz = true;
    if (enable_lz_probe) {
        if (stats) stats->filter_lo_lz_probe_enabled++;
        const auto& probe = get_lz_probe_runtime_params();
        if (lo_bytes.size() >= (size_t)probe.min_raw_bytes) {
            if (stats) stats->filter_lo_lz_probe_checked++;
            const size_t probe_n = std::min(lo_bytes.size(), (size_t)probe.sample_bytes);
            std::vector<uint8_t> sample(lo_bytes.begin(), lo_bytes.begin() + probe_n);
            auto sample_lz = compress_lz(sample);
            const size_t sample_wrapped = 6 + sample_lz.size();
            if (stats) {
                stats->filter_lo_lz_probe_sample_bytes_sum += probe_n;
                stats->filter_lo_lz_probe_sample_lz_bytes_sum += sample_lz.size();
                stats->filter_lo_lz_probe_sample_wrapped_bytes_sum += sample_wrapped;
            }
            if ((uint64_t)sample_wrapped * 1000ull >
                (uint64_t)probe_n * (uint64_t)probe.threshold_permille) {
                evaluate_lz = false;
                if (stats) stats->filter_lo_lz_probe_skip++;
            } else {
                if (stats) stats->filter_lo_lz_probe_pass++;
            }
        }
    }

    Clock::time_point t_mode2_eval0 = Clock::now();
    if (allow_parallel_base) {
        fut_legacy = worker_pool.submit([&]() {
            thread_budget::ScopedParallelRegion guard;
            return encode_byte_stream(lo_bytes);
        });
        if (evaluate_lz) {
            fut_lz = worker_pool.submit([&]() {
                thread_budget::ScopedParallelRegion guard;
                return compress_lz(lo_bytes);
            });
        }
    } else {
        lo_legacy = encode_byte_stream(lo_bytes);
    }

    if (stats) stats->filter_lo_raw_bytes_sum += lo_bytes.size();

    constexpr int kFilterLoModeWrapperGainPermilleDefault = 990;
    constexpr size_t kByteStreamMinEncodedBytes = 4 + (256 * 4) + 4 + 4; // cdf_size+cdf+count+rans_size

    // Mode5 runtime parameters (env-configurable)
    const auto& mode5_params = get_mode5_runtime_params();

    // Mode6 runtime parameters (env-configurable)
    const auto& mode6_params = get_mode6_runtime_params();
    const auto& mode7_params = get_mode7_runtime_params();

    std::vector<uint8_t> delta_bytes(lo_bytes.size());
    delta_bytes[0] = lo_bytes[0];
    for (size_t i = 1; i < lo_bytes.size(); i++) {
        delta_bytes[i] = (uint8_t)(lo_bytes[i] - lo_bytes[i - 1]);
    }
    auto delta_rans = encode_byte_stream(delta_bytes);
    size_t delta_wrapped = 6 + delta_rans.size();

    if (allow_parallel_base) {
        lo_legacy = fut_legacy.get();
        if (evaluate_lz) {
            lo_lz = fut_lz.get();
        }
    } else {
        if (evaluate_lz) {
            lo_lz = compress_lz(lo_bytes);
        }
    }
    if (stats) {
        stats->filter_lo_mode2_eval_ns += ns_since(t_mode2_eval0, Clock::now());
    }

    size_t legacy_size = lo_legacy.size();
    size_t lz_wrapped = std::numeric_limits<size_t>::max();
    if (evaluate_lz) {
        lz_wrapped = 6 + lo_lz.size();
    }

    std::vector<uint8_t> lo_lz_rans;
    size_t lz_rans_wrapped = std::numeric_limits<size_t>::max();
    if (evaluate_lz &&
        lo_bytes.size() >= (size_t)mode5_params.min_raw_bytes &&
        lo_lz.size() >= (size_t)mode5_params.min_lz_bytes) {
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

    if (evaluate_lz &&
        lz_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
        if (stats) stats->filter_lo_mode2_candidate_bytes_sum += lz_wrapped;
        if (lz_wrapped < best_size) {
            best_size = lz_wrapped;
            best_mode = 2;
        }
    } else {
        if (stats) stats->filter_lo_mode2_reject_gate++;
    }

    if (lz_rans_wrapped != std::numeric_limits<size_t>::max()) {
        bool better_than_legacy = (lz_rans_wrapped * 1000 <= legacy_size * (size_t)mode5_params.gain_permille);
        bool better_than_lz = (lz_rans_wrapped * 1000 <= lz_wrapped * (size_t)mode5_params.vs_lz_permille);

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

    // Mode6: Token-RANS - parse LZ into tokens and entropy code each stream
    std::vector<uint8_t> lo_mode6_encoded;
    size_t mode6_wrapped = std::numeric_limits<size_t>::max();
    uint32_t mode6_token_count = 0;
    bool mode6_considered = false;
    bool mode6_parse_ok = false;
    const bool mode6_enable = get_mode6_enable();
    if (mode6_enable && evaluate_lz &&
        lo_bytes.size() >= (size_t)mode6_params.min_raw_bytes &&
        lo_lz.size() >= (size_t)mode6_params.min_lz_bytes) {
        mode6_considered = true;
        if (stats) stats->filter_lo_mode6_candidates++;
        const auto t_mode6_eval0 = Clock::now();

        // Mode6 v0x0017: type bitpack + len split
        std::vector<uint8_t> type_bits, lit_len, match_len, dist_lo_stream, dist_hi_stream, lit_stream;
        uint32_t mode6_lit_token_count = 0;
        uint32_t mode6_match_count = 0;
        mode6_parse_ok = parse_tilelz_to_tokens_v17(
            lo_lz, type_bits, lit_len, match_len, dist_lo_stream, dist_hi_stream, lit_stream,
            mode6_token_count, mode6_lit_token_count, mode6_match_count
        );

        if (mode6_parse_ok) {
            // Sanity check: token_count == lit_token_count + match_count
            if (mode6_token_count != mode6_lit_token_count + mode6_match_count) {
                mode6_parse_ok = false;
                if (stats) stats->filter_lo_mode6_malformed_input++;
            } else {
                auto type_bits_enc = encode_byte_stream_shared_lz(type_bits);
                auto lit_len_enc = encode_byte_stream_shared_lz(lit_len);
                auto match_len_enc = encode_byte_stream_shared_lz(match_len);
                auto dist_lo_enc = encode_byte_stream_shared_lz(dist_lo_stream);
                auto dist_hi_enc = encode_byte_stream_shared_lz(dist_hi_stream);
                auto lit_enc = encode_byte_stream_shared_lz(lit_stream);

                // Payload v0x0017: [magic=0xAB][mode=6][raw_count][token_count][match_count][lit_token_count]
                //                  [type_bits_sz][lit_len_sz][match_len_sz][dist_lo_sz][dist_hi_sz][lit_sz]
                //                  [type_bits_enc][lit_len_enc][match_len_enc][dist_lo_enc][dist_hi_enc][lit_enc]
                // Header: 2 + 4 + 4 + 4 + 4 + 4*6 = 2 + 16 + 24 = 42 bytes minimum
                size_t header_size = 2 + 4 + 4 + 4 + 4 + 4 * 6; // magic+mode + raw_count + token_count + match_count + lit_token_count + 6 stream sizes
                mode6_wrapped = header_size + type_bits_enc.size() + lit_len_enc.size() + match_len_enc.size()
                                + dist_lo_enc.size() + dist_hi_enc.size() + lit_enc.size();

                if (mode6_wrapped < best_size) {
                    lo_mode6_encoded.clear();
                    lo_mode6_encoded.reserve(mode6_wrapped);
                    lo_mode6_encoded.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
                    lo_mode6_encoded.push_back(6);
                    uint32_t rc = (uint32_t)lo_bytes.size();
                    lo_mode6_encoded.push_back((uint8_t)(rc & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((rc >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((rc >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((rc >> 24) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)(mode6_token_count & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_token_count >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_token_count >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_token_count >> 24) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)(mode6_match_count & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_match_count >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_match_count >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_match_count >> 24) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)(mode6_lit_token_count & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_lit_token_count >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_lit_token_count >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_lit_token_count >> 24) & 0xFF));
                    auto push_size = [&](size_t sz) {
                        lo_mode6_encoded.push_back((uint8_t)(sz & 0xFF));
                        lo_mode6_encoded.push_back((uint8_t)((sz >> 8) & 0xFF));
                        lo_mode6_encoded.push_back((uint8_t)((sz >> 16) & 0xFF));
                        lo_mode6_encoded.push_back((uint8_t)((sz >> 24) & 0xFF));
                    };
                    push_size(type_bits_enc.size());
                    push_size(lit_len_enc.size());
                    push_size(match_len_enc.size());
                    push_size(dist_lo_enc.size());
                    push_size(dist_hi_enc.size());
                    push_size(lit_enc.size());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), type_bits_enc.begin(), type_bits_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), lit_len_enc.begin(), lit_len_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), match_len_enc.begin(), match_len_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), dist_lo_enc.begin(), dist_lo_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), dist_hi_enc.begin(), dist_hi_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), lit_enc.begin(), lit_enc.end());
                }

                if (stats) {
                    stats->filter_lo_mode6_match_tokens_sum += mode6_match_count;
                    stats->filter_lo_mode6_lit_tokens_sum += mode6_lit_token_count;
                    stats->filter_lo_mode6_token_count_sum += mode6_token_count;
                    stats->filter_lo_mode6_match_count_sum += mode6_match_count;
                    // Calculate dist bytes saved by compact format vs legacy
                    uint64_t dist_saved = (uint64_t)mode6_lit_token_count * 2; // 2 bytes per LIT token
                    stats->filter_lo_mode6_dist_saved_bytes_sum += dist_saved;
                    // v0x0017 specific counters
                    stats->filter_lo_mode6_typebits_raw_bytes_sum += type_bits.size();
                    stats->filter_lo_mode6_typebits_enc_bytes_sum += type_bits_enc.size();
                    stats->filter_lo_mode6_lit_len_bytes_sum += lit_len_enc.size();
                    stats->filter_lo_mode6_match_len_bytes_sum += match_len_enc.size();
                }
            }
        } else {
            if (stats) stats->filter_lo_mode6_malformed_input++;
        }

        if (stats) {
            stats->filter_lo_mode6_eval_ns += ns_since(t_mode6_eval0, Clock::now());
            stats->filter_lo_mode6_candidate_bytes_sum += lo_lz.size();
            if (mode6_wrapped != std::numeric_limits<size_t>::max()) {
                stats->filter_lo_mode6_wrapped_bytes_sum += mode6_wrapped;
            }
            stats->filter_lo_mode6_legacy_bytes_sum += legacy_size;
        }
    }

    if (mode6_wrapped != std::numeric_limits<size_t>::max()) {
        bool better_than_legacy = (mode6_wrapped * 1000 <= legacy_size * (size_t)mode6_params.gain_permille);
        bool better_than_lz = (mode6_wrapped * 1000 <= lz_wrapped * (size_t)mode6_params.vs_lz_permille);

        if (better_than_legacy && better_than_lz) {
            if (mode6_wrapped < best_size) {
                best_size = mode6_wrapped;
                best_mode = 6;
                if (stats) stats->filter_lo_mode6_v17_selected++;
            } else {
                if (stats) stats->filter_lo_mode6_reject_best++;
            }
        } else {
            if (stats) stats->filter_lo_mode6_reject_gate++;
        }
    }

    if (stats && mode6_considered && best_mode != 6) {
        if (best_mode == 5) stats->filter_lo_mode6_fallback_to_mode5++;
        else if (best_mode == 0) stats->filter_lo_mode6_fallback_to_mode0++;
    }

    std::vector<uint8_t> pred_stream;
    std::vector<uint8_t> resid_stream;
    std::vector<uint8_t> mode3_preds;
    std::vector<int> row_lens;

    std::vector<std::vector<uint8_t>> mode4_streams(6);
    std::vector<uint32_t> mode4_ctx_raw_counts(6, 0);
    std::vector<std::vector<uint8_t>> mode7_streams(6);
    std::vector<uint32_t> mode7_ctx_raw_counts(6, 0);
    uint32_t mode7_shared_mask = 0;
    // Mode8 output variables (declared here for use in output stage)
    std::vector<std::vector<uint8_t>> mode8_output_streams;
    std::vector<uint8_t> mode8_output_codec_ids;
    std::vector<uint32_t> mode8_output_ctx_raw_counts;

    const bool enable_mode3_mode4 = ((profile_code == 1 || profile_code == 2) && lo_bytes.size() > 256);
    if (enable_mode3_mode4) {
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
        const size_t mode3_min_size = 1 + 1 + 4 + 4 + (2 * kByteStreamMinEncodedBytes);
        const bool mode3_lower_bound_reject =
            (mode3_min_size >= best_size) ||
            ((uint64_t)mode3_min_size * 1000ull >
             (uint64_t)legacy_size * (uint64_t)kFilterLoModeWrapperGainPermilleDefault);

        if (!mode3_lower_bound_reject) {
            const auto t_mode3_eval0 = Clock::now();
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
        int nonempty_ctx = 0;
        for (int k = 0; k < 6; k++) {
            if (!lo_ctx[k].empty()) nonempty_ctx++;
        }
        const size_t mode4_min_size = mode4_sz + (size_t)nonempty_ctx * kByteStreamMinEncodedBytes;
        const bool mode4_lower_bound_reject =
            (nonempty_ctx < 2) ||
            (mode4_min_size >= best_size) ||
            ((uint64_t)mode4_min_size * 1000ull >
             (uint64_t)legacy_size * (uint64_t)kFilterLoModeWrapperGainPermilleDefault);

        const size_t mode4_gate_limit =
            (legacy_size * (size_t)kFilterLoModeWrapperGainPermilleDefault + 999u) / 1000u;
        std::vector<std::vector<uint8_t>> ctx_streams(6);
        std::vector<uint32_t> ctx_raw_counts(6, 0);
        bool mode4_aborted = mode4_lower_bound_reject;
        thread_budget::ScopedThreadTokens ctx_parallel_tokens;
        if (!mode4_aborted && hw_threads >= 6 && lo_bytes.size() >= 8192) {
            ctx_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(6);
        }
        const bool allow_parallel_ctx = (!mode4_aborted && ctx_parallel_tokens.acquired());
        if (allow_parallel_ctx) {
            std::vector<std::future<std::vector<uint8_t>>> futs(6);
            std::vector<bool> launched(6, false);
            for (int k = 0; k < 6; k++) {
                ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
                if (lo_ctx[k].empty()) continue;
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
        } else if (!mode4_aborted) {
            for (int k = 0; k < 6; k++) {
                ctx_raw_counts[k] = (uint32_t)lo_ctx[k].size();
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
                mode4_streams = ctx_streams;
                mode4_ctx_raw_counts = ctx_raw_counts;
            }
        } else if (nonempty_ctx >= 2) {
            if (stats) stats->filter_lo_mode4_reject_gate++;
        }
        if (stats) stats->filter_lo_mode4_eval_ns += ns_since(t_mode4_eval0, Clock::now());

        // Mode7: context-split wrapper with per-context coder selection.
        // Each context chooses smaller of: legacy adaptive CDF vs shared LZ CDF.
        const bool mode7_enable = get_mode7_enable();
        if (mode7_enable && !mode4_aborted && nonempty_ctx >= 2) {
            if (stats) stats->filter_lo_mode7_candidates++;
            const auto t_mode7_eval0 = Clock::now();

            std::vector<std::vector<uint8_t>> mode7_candidate_streams = ctx_streams;
            std::vector<uint32_t> mode7_candidate_ctx_raw_counts = ctx_raw_counts;
            uint32_t mode7_candidate_shared_mask = 0;
            uint64_t mode7_shared_ctx_count = 0;
            size_t mode7_sz = 1 + 1 + 4 + 4 + 6 * 4;

            for (int k = 0; k < 6; k++) {
                if (lo_ctx[k].empty()) continue;
                auto& selected_stream = mode7_candidate_streams[k];
                if (lo_ctx[k].size() >= (size_t)mode7_params.min_ctx_bytes) {
                    auto shared_stream = encode_byte_stream_shared_lz(lo_ctx[k]);
                    if (shared_stream.size() < selected_stream.size()) {
                        selected_stream = std::move(shared_stream);
                        mode7_candidate_shared_mask |= (1u << k);
                        mode7_shared_ctx_count++;
                    }
                }
                mode7_sz += selected_stream.size();
            }

            if (stats) {
                stats->filter_lo_mode7_eval_ns += ns_since(t_mode7_eval0, Clock::now());
                stats->filter_lo_mode7_wrapped_bytes_sum += mode7_sz;
                stats->filter_lo_mode7_legacy_bytes_sum += legacy_size;
                stats->filter_lo_mode7_shared_ctx_sum += mode7_shared_ctx_count;
            }

            const bool mode7_better_than_legacy =
                (mode7_sz * 1000 <= legacy_size * (size_t)mode7_params.gain_permille);
            const bool mode7_better_than_mode4 =
                (mode4_sz != std::numeric_limits<size_t>::max()) &&
                (mode7_sz * 1000 <= mode4_sz * (size_t)mode7_params.vs_mode4_permille);

            if (mode7_better_than_legacy && mode7_better_than_mode4) {
                if (mode7_sz < best_size) {
                    best_size = mode7_sz;
                    best_mode = 7;
                    mode7_streams = std::move(mode7_candidate_streams);
                    mode7_ctx_raw_counts = std::move(mode7_candidate_ctx_raw_counts);
                    mode7_shared_mask = mode7_candidate_shared_mask;
                } else {
                    if (stats) stats->filter_lo_mode7_reject_best++;
                }
            } else {
                if (stats) stats->filter_lo_mode7_reject_gate++;
            }
        }

        // Mode8: context-split wrapper with per-context hybrid codec selection.
        // Each context chooses smallest of: legacy rANS / delta+rANS / LZ+rANS(shared).
        const bool mode8_enable = get_mode8_enable();
        const bool mode4_valid = !mode4_aborted && nonempty_ctx >= 2;
        if (mode8_enable && mode4_valid) {
            if (stats) stats->filter_lo_mode8_candidates++;
            const auto t_mode8_eval0 = Clock::now();
            const auto& mode8_params = get_mode8_runtime_params();

            std::vector<std::vector<uint8_t>> mode8_streams(6);
            std::vector<uint8_t> mode8_codec_ids(6, 255);  // 0=legacy, 1=delta, 2=lz, 255=empty
            std::vector<uint32_t> mode8_ctx_raw_counts = ctx_raw_counts;
            uint64_t mode8_ctx_legacy = 0, mode8_ctx_delta = 0, mode8_ctx_lz = 0;
            size_t mode8_sz = 1 + 1 + 4 + 6 + 6 * 4;  // header + codec_ids + lens
            bool mode8_aborted = false;

            for (int k = 0; k < 6 && !mode8_aborted; k++) {
                if (lo_ctx[k].empty()) {
                    mode8_codec_ids[k] = 255;
                    continue;
                }

                // Candidate 0: legacy rANS
                auto legacy_stream = encode_byte_stream(lo_ctx[k]);
                size_t best_ctx_sz = legacy_stream.size();
                uint8_t best_codec = 0;

                // Candidate 1: delta + rANS
                std::vector<uint8_t> delta_data(lo_ctx[k].size());
                delta_data[0] = lo_ctx[k][0];
                for (size_t i = 1; i < lo_ctx[k].size(); i++) {
                    delta_data[i] = (uint8_t)(lo_ctx[k][i] - lo_ctx[k][i - 1]);
                }
                auto delta_stream = encode_byte_stream(delta_data);
                if (delta_stream.size() < best_ctx_sz) {
                    best_ctx_sz = delta_stream.size();
                    best_codec = 1;
                    mode8_streams[k] = std::move(delta_stream);
                } else {
                    mode8_streams[k] = std::move(legacy_stream);
                }

                // Candidate 2: LZ + rANS(shared) - only for larger contexts
                if (lo_ctx[k].size() >= (size_t)mode8_params.min_ctx_bytes) {
                    auto ctx_lz = compress_lz(lo_ctx[k]);
                    if (!ctx_lz.empty()) {
                        auto lz_stream = encode_byte_stream_shared_lz(ctx_lz);
                        if (lz_stream.size() < best_ctx_sz) {
                            best_ctx_sz = lz_stream.size();
                            best_codec = 2;
                            mode8_streams[k] = std::move(lz_stream);
                        }
                    }
                }

                mode8_codec_ids[k] = best_codec;
                mode8_sz += best_ctx_sz;

                // Track codec selection stats
                if (best_codec == 0) mode8_ctx_legacy++;
                else if (best_codec == 1) mode8_ctx_delta++;
                else if (best_codec == 2) mode8_ctx_lz++;

                // Early abort if size exceeds limits
                if (mode8_sz > best_size) {
                    mode8_aborted = true;
                }
            }

            if (stats) {
                stats->filter_lo_mode8_eval_ns += ns_since(t_mode8_eval0, Clock::now());
                if (!mode8_aborted) {
                    stats->filter_lo_mode8_wrapped_bytes_sum += mode8_sz;
                    stats->filter_lo_mode8_ctx_legacy_sum += mode8_ctx_legacy;
                    stats->filter_lo_mode8_ctx_delta_sum += mode8_ctx_delta;
                    stats->filter_lo_mode8_ctx_lz_sum += mode8_ctx_lz;
                }
            }

            if (!mode8_aborted) {
                const bool mode8_better_than_legacy =
                    (mode8_sz * 1000 <= legacy_size * (size_t)mode8_params.gain_permille);
                const bool mode8_better_than_mode4 =
                    (mode8_sz * 1000 <= mode4_sz * (size_t)mode8_params.vs_mode4_permille);

                if (mode8_better_than_legacy && mode8_better_than_mode4) {
                    if (mode8_sz < best_size) {
                        best_size = mode8_sz;
                        best_mode = 8;
                        // Store mode8 streams for final output
                        mode8_output_streams = std::move(mode8_streams);
                        mode8_output_codec_ids = std::move(mode8_codec_ids);
                        mode8_output_ctx_raw_counts = std::move(mode8_ctx_raw_counts);
                    } else {
                        if (stats) stats->filter_lo_mode8_reject_best++;
                    }
                } else {
                    if (stats) stats->filter_lo_mode8_reject_gate++;
                }
            }
        }
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
    } else if (best_mode == 7) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(7);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
        lo_stream.push_back((uint8_t)(mode7_shared_mask & 0xFF));
        lo_stream.push_back((uint8_t)((mode7_shared_mask >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((mode7_shared_mask >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((mode7_shared_mask >> 24) & 0xFF));
        for (int k = 0; k < 6; k++) {
            uint32_t len = (uint32_t)mode7_streams[k].size();
            lo_stream.push_back((uint8_t)(len & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
        }
        for (int k = 0; k < 6; k++) {
            lo_stream.insert(lo_stream.end(), mode7_streams[k].begin(), mode7_streams[k].end());
        }
        if (stats) {
            stats->filter_lo_mode7++;
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode7_saved_bytes_sum +=
                    (uint64_t)(lo_legacy.size() - lo_stream.size());
            }
            int nonempty_ctx = 0;
            for (int k = 0; k < 6; k++) {
                stats->filter_lo_ctx_bytes_sum[k] += mode7_ctx_raw_counts[k];
                if (mode7_ctx_raw_counts[k] > 0) nonempty_ctx++;
            }
            if (nonempty_ctx > 0) {
                stats->filter_lo_ctx_nonempty_tiles++;
            }
        }
    } else if (best_mode == 8) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(8);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
        // Write ctx_codec_ids[6]
        for (int k = 0; k < 6; k++) {
            lo_stream.push_back(mode8_output_codec_ids[k]);
        }
        // Write lens[6]
        for (int k = 0; k < 6; k++) {
            uint32_t len = (uint32_t)mode8_output_streams[k].size();
            lo_stream.push_back((uint8_t)(len & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
        }
        // Write ctx streams
        for (int k = 0; k < 6; k++) {
            lo_stream.insert(lo_stream.end(), mode8_output_streams[k].begin(), mode8_output_streams[k].end());
        }
        if (stats) {
            stats->filter_lo_mode8++;
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode8_saved_bytes_sum +=
                    (uint64_t)(lo_legacy.size() - lo_stream.size());
            }
            int nonempty_ctx = 0;
            for (int k = 0; k < 6; k++) {
                stats->filter_lo_ctx_bytes_sum[k] += mode8_output_ctx_raw_counts[k];
                if (mode8_output_ctx_raw_counts[k] > 0) nonempty_ctx++;
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
        } else if (best_mode == 6) {
            lo_stream = std::move(lo_mode6_encoded);
            if (stats) {
                stats->filter_lo_mode6++;
                if (lo_legacy.size() > lo_stream.size()) {
                    stats->filter_lo_mode6_saved_bytes_sum +=
                        (uint64_t)(lo_legacy.size() - lo_stream.size());
                }
            }
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
