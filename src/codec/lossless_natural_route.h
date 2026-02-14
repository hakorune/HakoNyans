#pragma once

#include "headers.h"
#include "lz_tile.h"
#include "lossless_filter.h"
#include "lossless_mode_debug_stats.h"
#include "../platform/thread_budget.h"
#include "zigzag.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <future>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace hakonyans::lossless_natural_route {

namespace detail {

#include "lossless_natural_route_lz_impl.h"

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn>
inline std::vector<uint8_t> build_mode0_payload(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val, EncodeSharedLzFn&& encode_byte_stream_shared_lz
) {
    std::vector<uint8_t> row_pred_ids(pad_h, 0);
    std::vector<uint8_t> residual_bytes;
    residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = residual_bytes.data();

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int up = up_row ? (int)up_row[x] : 0;
            cost0 += (uint64_t)std::abs(cur);
            cost1 += (uint64_t)std::abs(cur - up);
            cost2 += (uint64_t)std::abs(cur - (up / 2));
        }

        int best_p = 0;
        uint64_t best_cost = cost0;
        if (cost1 < best_cost) {
            best_cost = cost1;
            best_p = 1;
        }
        if (cost2 < best_cost) {
            best_p = 2;
        }
        row_pred_ids[y] = (uint8_t)best_p;

        if (best_p == 0) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t left = (x > 0) ? row[x - 1] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)left);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else if (best_p == 1) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t up = up_row ? up_row[x] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)up);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t left = (x > 0) ? row[x - 1] : 0;
                const int16_t up = up_row ? up_row[x] : 0;
                const int16_t pred = (int16_t)(((int)left + (int)up) / 2);
                const int16_t resid = (int16_t)((int)row[x] - (int)pred);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        }
    }

    auto resid_lz = TileLZ::compress(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    // [magic][mode=0][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4][pred_ids][payload]
    std::vector<uint8_t> out;
    out.reserve(18 + row_pred_ids.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(0);
    uint32_t pred_count = pad_h;
    uint32_t resid_raw_count = (uint32_t)residual_bytes.size();
    uint32_t resid_payload_size = (uint32_t)resid_lz_rans.size();
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pred_count);
    push_u32(resid_raw_count);
    push_u32(resid_payload_size);
    out.insert(out.end(), row_pred_ids.begin(), row_pred_ids.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

struct Mode1Prepared {
    std::vector<uint8_t> row_pred_ids;
    std::vector<int16_t> residuals;
    std::vector<uint8_t> residual_bytes;
};

struct PackedPredictorStream {
    uint8_t mode = 0; // 0=raw, 1=rANS
    std::vector<uint8_t> payload;
    bool valid = false;
};

inline size_t mode12_min_candidate_size(const PackedPredictorStream& packed_pred) {
    if (!packed_pred.valid || packed_pred.payload.empty()) {
        return std::numeric_limits<size_t>::max();
    }
    // mode1/mode2 wrapper fixed header (27 bytes) + pred payload + residual payload (>=1 byte)
    return 27u + packed_pred.payload.size() + 1u;
}

template <typename EncodeByteStreamFn>
inline PackedPredictorStream build_packed_predictor_stream(
    const std::vector<uint8_t>& row_pred_ids,
    EncodeByteStreamFn&& encode_byte_stream
) {
    PackedPredictorStream out;
    if (row_pred_ids.empty()) return out;

    out.payload = row_pred_ids;
    auto pred_rans = encode_byte_stream(row_pred_ids);
    if (!pred_rans.empty() && pred_rans.size() < out.payload.size()) {
        out.payload = std::move(pred_rans);
        out.mode = 1;
    }
    out.valid = true;
    return out;
}

template <typename ZigzagEncodeFn>
inline Mode1Prepared build_mode1_prepared(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val
) {
    Mode1Prepared prepared;
    prepared.row_pred_ids.resize(pad_h, 0);
    prepared.residuals.resize(pixel_count);
    prepared.residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = prepared.residual_bytes.data();

    std::vector<int16_t> recon(pixel_count, 0);

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        uint64_t cost3 = 0; // PAETH
        uint64_t cost4 = 0; // MED
        uint64_t cost5 = 0; // WEIGHTED_A
        uint64_t cost6 = 0; // WEIGHTED_B
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int16_t b = up_row ? up_row[x] : 0;
            const int16_t c = (up_row && x > 0) ? up_row[x - 1] : 0;
            const int16_t a = (x > 0) ? row[x - 1] : 0;
            const int pred2 = ((int)a + (int)b) / 2;
            const int pred3 = (int)LosslessFilter::paeth_predictor(a, b, c);
            const int pred4 = (int)LosslessFilter::med_predictor(a, b, c);
            const int pred5 = ((int)a * 3 + (int)b) / 4;
            const int pred6 = ((int)a + (int)b * 3) / 4;
            cost0 += (uint64_t)std::abs(cur - (int)a);
            cost1 += (uint64_t)std::abs(cur - (int)b);
            cost2 += (uint64_t)std::abs(cur - pred2);
            cost3 += (uint64_t)std::abs(cur - pred3);
            cost4 += (uint64_t)std::abs(cur - pred4);
            cost5 += (uint64_t)std::abs(cur - pred5);
            cost6 += (uint64_t)std::abs(cur - pred6);
        }

        int best_p = 0;
        uint64_t best_cost = cost0;
        if (cost1 < best_cost) {
            best_cost = cost1;
            best_p = 1;
        }
        if (cost2 < best_cost) {
            best_cost = cost2;
            best_p = 2;
        }
        if (cost3 < best_cost) {
            best_cost = cost3;
            best_p = 3;
        }
        if (cost4 < best_cost) {
            best_cost = cost4;
            best_p = 4;
        }
        if (cost5 < best_cost) {
            best_cost = cost5;
            best_p = 5;
        }
        if (cost6 < best_cost) {
            best_p = 6;
        }
        prepared.row_pred_ids[y] = (uint8_t)best_p;

        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (best_p == 0) pred = a;
            else if (best_p == 1) pred = b;
            else if (best_p == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (best_p == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else if (best_p == 4) pred = LosslessFilter::med_predictor(a, b, c);
            else if (best_p == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
            else pred = (int16_t)(((int)a + (int)b * 3) / 4);

            int16_t cur = row[x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);
            prepared.residuals[(size_t)y * pad_w + x] = resid;

            uint16_t zz = zigzag_encode_val(resid);
            resid_dst[0] = (uint8_t)(zz & 0xFF);
            resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
            resid_dst += 2;
        }
    }
    return prepared;
}

template <typename Mode1PreparedT,
          typename EncodeSharedLzFn,
          typename CompressResidualFn>
inline std::vector<uint8_t> build_mode1_payload_from_prepared(
    const Mode1PreparedT& prepared,
    const PackedPredictorStream& packed_pred,
    uint32_t pad_h,
    uint32_t pixel_count,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    uint8_t out_mode,
    CompressResidualFn&& compress_residual
) {
    const auto& residual_bytes = prepared.residual_bytes;
    if (!packed_pred.valid || packed_pred.payload.empty() || residual_bytes.empty()) return {};

    auto resid_lz = compress_residual(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    // [magic][mode=1/2][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][resid_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + packed_pred.payload.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(out_mode);
    uint32_t pred_count = pad_h;
    uint32_t resid_raw_count = (uint32_t)residual_bytes.size();
    uint32_t resid_payload_size = (uint32_t)resid_lz_rans.size();
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pred_count);
    push_u32(resid_raw_count);
    push_u32(resid_payload_size);
    out.push_back(packed_pred.mode);
    push_u32(pred_count);
    push_u32((uint32_t)packed_pred.payload.size());
    out.insert(out.end(), packed_pred.payload.begin(), packed_pred.payload.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

template <typename Mode1PreparedT,
          typename PackedPredictorStreamT,
          typename EncodeByteStreamFn>
inline std::vector<uint8_t> build_mode3_payload_from_prepared(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    const Mode1PreparedT& prepared,
    const PackedPredictorStreamT& packed_pred,
    EncodeByteStreamFn&& encode_byte_stream
) {
    if (!packed_pred.valid || packed_pred.payload.empty()) return {};

    std::vector<uint8_t> flat_bytes;
    std::vector<uint8_t> edge_bytes;
    flat_bytes.reserve((size_t)pixel_count * 2);
    edge_bytes.reserve((size_t)pixel_count * 2);

    const std::vector<uint8_t>& pred_ids = prepared.row_pred_ids;
    std::vector<int16_t> recon(pixel_count, 0);

    for (uint32_t y = 0; y < pad_h; y++) {
        uint8_t pid = pred_ids[y];
        const int16_t* padded_row = padded + (size_t)y * pad_w;
        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (pid == 0) pred = a;
            else if (pid == 1) pred = b;
            else if (pid == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (pid == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else if (pid == 4) pred = LosslessFilter::med_predictor(a, b, c);
            else if (pid == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
            else if (pid == 6) pred = (int16_t)(((int)a + (int)b * 3) / 4);

            int16_t cur = padded_row[x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);

            uint16_t zz = zigzag_encode_val(resid);
            
            int grad = std::max(std::abs(a - c), std::abs(b - c));
            if (grad < 16) {
                flat_bytes.push_back((uint8_t)(zz & 0xFF));
                flat_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
            } else {
                edge_bytes.push_back((uint8_t)(zz & 0xFF));
                edge_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
            }
        }
    }

    auto flat_rans = encode_byte_stream(flat_bytes);
    auto edge_rans = encode_byte_stream(edge_bytes);

    // [magic][mode=3][pixel_count:4][pred_count:4][flat_payload_size:4][edge_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][flat_payload][edge_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + packed_pred.payload.size() + flat_rans.size() + edge_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(3);
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pad_h); // pred_count
    push_u32((uint32_t)flat_rans.size());
    push_u32((uint32_t)edge_rans.size());
    out.push_back(packed_pred.mode);
    push_u32(pad_h); // pred_raw_count
    push_u32((uint32_t)packed_pred.payload.size());
    out.insert(out.end(), packed_pred.payload.begin(), packed_pred.payload.end());
    out.insert(out.end(), flat_rans.begin(), flat_rans.end());
    out.insert(out.end(), edge_rans.begin(), edge_rans.end());
    return out;
}

} // namespace detail

// Natural/photo-oriented route:
// mode0: row SUB/UP/AVG + residual LZ+rANS(shared CDF)
// mode1: row SUB/UP/AVG/PAETH/MED + compressed predictor stream
// mode2: mode1 predictor set + natural-only global-chain LZ for residual stream
// mode3: mode1 predictor set + 2-context adaptive rANS (flat/edge)
template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile_padded(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr,
    int mode2_nice_length_override = -1,
    int mode2_match_strategy_override = -1
) {
    using Clock = std::chrono::steady_clock;
    auto ns_since = [](const Clock::time_point& t0, const Clock::time_point& t1) -> uint64_t {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };
    if (!padded || pad_w == 0 || pad_h == 0) return {};
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) return {};

    auto lz_params = detail::global_chain_lz_runtime_params();
    if (mode2_nice_length_override >= 4 && mode2_nice_length_override <= 255) {
        lz_params.nice_length = mode2_nice_length_override;
    }
    if (mode2_match_strategy_override >= 0 && mode2_match_strategy_override <= 2) {
        lz_params.match_strategy = mode2_match_strategy_override;
    }
    std::vector<uint8_t> mode0;
    std::vector<uint8_t> mode1;
    std::vector<uint8_t> mode2;
    std::vector<uint8_t> mode3;
    detail::Mode1Prepared mode1_prepared;
    detail::PackedPredictorStream mode1_pred;
    auto accumulate_mode2_lz = [&](const detail::GlobalChainLzCounters& c) {
        if (!stats) return;
        stats->natural_row_mode2_lz_calls += c.calls;
        stats->natural_row_mode2_lz_src_bytes_sum += c.src_bytes;
        stats->natural_row_mode2_lz_out_bytes_sum += c.out_bytes;
        stats->natural_row_mode2_lz_match_count += c.match_count;
        stats->natural_row_mode2_lz_match_bytes_sum += c.match_bytes;
        stats->natural_row_mode2_lz_literal_bytes_sum += c.literal_bytes;
        stats->natural_row_mode2_lz_chain_steps_sum += c.chain_steps;
        stats->natural_row_mode2_lz_depth_limit_hits += c.depth_limit_hits;
        stats->natural_row_mode2_lz_early_maxlen_hits += c.early_maxlen_hits;
        stats->natural_row_mode2_lz_nice_cutoff_hits += c.nice_cutoff_hits;
        stats->natural_row_mode2_lz_len3_reject_dist += c.len3_reject_dist;
        stats->natural_row_mode2_lz_optparse_enabled += c.optparse_enabled;
        stats->natural_row_mode2_lz_optparse_fallback_count += c.optparse_fallback_count;
        stats->natural_row_mode2_lz_optparse_fallback_memcap += c.optparse_fallback_memcap;
        stats->natural_row_mode2_lz_optparse_fallback_allocfail += c.optparse_fallback_allocfail;
        stats->natural_row_mode2_lz_optparse_fallback_unreachable +=
            c.optparse_fallback_unreachable;
        stats->natural_row_mode2_lz_optparse_dp_positions_sum += c.optparse_dp_positions;
        stats->natural_row_mode2_lz_optparse_lit_edges_sum += c.optparse_lit_edges_eval;
        stats->natural_row_mode2_lz_optparse_match_edges_sum += c.optparse_match_edges_eval;
        stats->natural_row_mode2_lz_optparse_tokens_lit_sum += c.optparse_tokens_litrun;
        stats->natural_row_mode2_lz_optparse_tokens_match_sum += c.optparse_tokens_match;
        stats->natural_row_mode2_lz_optparse_shorter_than_longest_sum +=
            c.optparse_chose_shorter_than_longest;
        stats->natural_row_mode2_lz_optparse_probe_accept += c.optparse_probe_accept;
        stats->natural_row_mode2_lz_optparse_probe_reject += c.optparse_probe_reject;
        stats->natural_row_mode2_lz_optparse_adopt += c.optparse_adopt;
        stats->natural_row_mode2_lz_optparse_reject_small_gain +=
            c.optparse_reject_small_gain;
    };
    constexpr uint32_t kPrepParallelPixelThreshold = 262144u;
    thread_budget::ScopedThreadTokens pipeline_tokens;
    if (pixel_count >= kPrepParallelPixelThreshold) {
        pipeline_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(1);
    }

    if (pipeline_tokens.acquired()) {
        struct ReadyData {
            std::shared_ptr<const detail::Mode1Prepared> prepared;
            std::shared_ptr<const detail::PackedPredictorStream> pred;
            uint64_t prep_ns = 0;
            uint64_t pred_ns = 0;
        };
        struct TimedPayload {
            std::vector<uint8_t> payload;
            uint64_t elapsed_ns = 0;
            detail::GlobalChainLzCounters lz;
        };
        struct TimedMode23 {
            TimedPayload mode2;
            std::vector<uint8_t> mode3;
            uint64_t mode3_elapsed_ns = 0;
        };
        if (stats) {
            stats->natural_row_prep_parallel_count++;
            stats->natural_row_prep_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
            stats->natural_row_mode12_parallel_count++;
            stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)pipeline_tokens.count();
        }

        std::promise<ReadyData> ready_promise;
        auto ready_future = ready_promise.get_future();
        auto mode23_future = std::async(
            std::launch::async,
            [&,
             rp = std::move(ready_promise)]() mutable -> TimedMode23 {
                thread_budget::ScopedParallelRegion guard;
                ReadyData ready;
                const auto t_prep0 = Clock::now();
                auto prep_local = detail::build_mode1_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    zigzag_encode_val
                );
                const auto t_prep1 = Clock::now();
                ready.prep_ns = ns_since(t_prep0, t_prep1);

                const auto t_pred0 = Clock::now();
                auto pred_local = detail::build_packed_predictor_stream(
                    prep_local.row_pred_ids,
                    encode_byte_stream
                );
                const auto t_pred1 = Clock::now();
                ready.pred_ns = ns_since(t_pred0, t_pred1);
                ready.prepared = std::make_shared<const detail::Mode1Prepared>(std::move(prep_local));
                ready.pred = std::make_shared<const detail::PackedPredictorStream>(std::move(pred_local));

                rp.set_value(ready);

                const auto t_mode2_0 = Clock::now();
                TimedPayload out2;
                detail::GlobalChainLzCounters lz_counters;
                out2.payload = detail::build_mode1_payload_from_prepared(
                    *ready.prepared,
                    *ready.pred,
                    pad_h,
                    pixel_count,
                    encode_byte_stream_shared_lz,
                    2,
                    [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                        return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                    }
                );
                out2.lz = lz_counters;
                const auto t_mode2_1 = Clock::now();
                out2.elapsed_ns = ns_since(t_mode2_0, t_mode2_1);

                const auto t_mode3_0 = Clock::now();
                std::vector<uint8_t> out3 = detail::build_mode3_payload_from_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    *ready.prepared,
                    *ready.pred,
                    encode_byte_stream
                );
                const auto t_mode3_1 = Clock::now();

                return TimedMode23{
                    std::move(out2),
                    std::move(out3),
                    ns_since(t_mode3_0, t_mode3_1)
                };
            }
        );

        const auto t_mode0_0 = Clock::now();
        mode0 = detail::build_mode0_payload(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val, encode_byte_stream_shared_lz
        );
        const auto t_mode0_1 = Clock::now();
        if (stats) stats->natural_row_mode0_build_ns += ns_since(t_mode0_0, t_mode0_1);

        auto ready = ready_future.get();
        if (stats) {
            stats->natural_row_mode1_prepare_ns += ready.prep_ns;
            stats->natural_row_pred_pack_ns += ready.pred_ns;
            if (ready.pred->mode == 0) stats->natural_row_pred_mode_raw_count++;
            else stats->natural_row_pred_mode_rans_count++;
        }

        const auto t_mode1_0 = Clock::now();
        mode1 = detail::build_mode1_payload_from_prepared(
            *ready.prepared,
            *ready.pred,
            pad_h,
            pixel_count,
            encode_byte_stream_shared_lz,
            1,
            [](const std::vector<uint8_t>& bytes) {
                return TileLZ::compress(bytes);
            }
        );
        const auto t_mode1_1 = Clock::now();
        if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);

        auto mode23_res = mode23_future.get();
        mode2 = std::move(mode23_res.mode2.payload);
        mode3 = std::move(mode23_res.mode3);
        if (stats) {
            stats->natural_row_mode2_build_ns += mode23_res.mode2.elapsed_ns;
            stats->natural_row_mode3_build_ns += mode23_res.mode3_elapsed_ns;
        }
        accumulate_mode2_lz(mode23_res.mode2.lz);
    } else {
        if (stats) stats->natural_row_prep_seq_count++;
        const auto t_mode0_0 = Clock::now();
        mode0 = detail::build_mode0_payload(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val, encode_byte_stream_shared_lz
        );
        const auto t_mode0_1 = Clock::now();
        if (stats) stats->natural_row_mode0_build_ns += ns_since(t_mode0_0, t_mode0_1);
        const auto t_mode1p_0 = Clock::now();
        mode1_prepared = detail::build_mode1_prepared(
            padded, pad_w, pad_h, pixel_count,
            zigzag_encode_val
        );
        const auto t_mode1p_1 = Clock::now();
        if (stats) stats->natural_row_mode1_prepare_ns += ns_since(t_mode1p_0, t_mode1p_1);
        const auto t_pred0 = Clock::now();
        mode1_pred = detail::build_packed_predictor_stream(
            mode1_prepared.row_pred_ids,
            encode_byte_stream
        );
        const auto t_pred1 = Clock::now();
        if (stats) {
            stats->natural_row_pred_pack_ns += ns_since(t_pred0, t_pred1);
            if (mode1_pred.mode == 0) stats->natural_row_pred_mode_raw_count++;
            else stats->natural_row_pred_mode_rans_count++;
        }

        const size_t mode2_min_size = detail::mode12_min_candidate_size(mode1_pred);
        const uint64_t mode2_limit_vs_mode0 =
            ((uint64_t)mode0.size() * (uint64_t)lz_params.bias_permille) / 1000ull;
        const bool mode2_possible_vs_mode0 =
            (mode2_min_size != std::numeric_limits<size_t>::max()) &&
            (mode2_min_size <= mode2_limit_vs_mode0);

        constexpr uint32_t kMode12ParallelPixelThreshold = 262144u;
        thread_budget::ScopedThreadTokens mode12_tokens;
        if (pixel_count >= kMode12ParallelPixelThreshold) {
            mode12_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(1);
        }
        struct TimedPayload {
            std::vector<uint8_t> payload;
            uint64_t elapsed_ns = 0;
            detail::GlobalChainLzCounters lz;
        };
        struct TimedMode23 {
            TimedPayload mode2;
            std::vector<uint8_t> mode3;
            uint64_t mode3_elapsed_ns = 0;
        };
        if (mode12_tokens.acquired() && mode2_possible_vs_mode0) {
            if (stats) {
                stats->natural_row_mode12_parallel_count++;
                stats->natural_row_mode12_parallel_tokens_sum += (uint64_t)mode12_tokens.count();
            }
            auto f_mode23 = std::async(std::launch::async, [&]() -> TimedMode23 {
                thread_budget::ScopedParallelRegion guard;
                const auto t0 = Clock::now();
                TimedPayload out2;
                detail::GlobalChainLzCounters lz_counters;
                out2.payload = detail::build_mode1_payload_from_prepared(
                    mode1_prepared,
                    mode1_pred,
                    pad_h,
                    pixel_count,
                    encode_byte_stream_shared_lz,
                    2,
                    [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                        return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                    }
                );
                out2.lz = lz_counters;
                const auto t1 = Clock::now();
                out2.elapsed_ns = ns_since(t0, t1);

                const auto t_mode3_0 = Clock::now();
                std::vector<uint8_t> out3 = detail::build_mode3_payload_from_prepared(
                    padded, pad_w, pad_h, pixel_count,
                    mode1_prepared,
                    mode1_pred,
                    encode_byte_stream
                );
                const auto t_mode3_1 = Clock::now();
                return TimedMode23{
                    std::move(out2),
                    std::move(out3),
                    ns_since(t_mode3_0, t_mode3_1)
                };
            });
            const auto t_mode1_0 = Clock::now();
            mode1 = detail::build_mode1_payload_from_prepared(
                mode1_prepared,
                mode1_pred,
                pad_h,
                pixel_count,
                encode_byte_stream_shared_lz,
                1,
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                }
            );
            const auto t_mode1_1 = Clock::now();
            if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);
            auto mode23_res = f_mode23.get();
            mode2 = std::move(mode23_res.mode2.payload);
            mode3 = std::move(mode23_res.mode3);
            if (stats) {
                stats->natural_row_mode2_build_ns += mode23_res.mode2.elapsed_ns;
                stats->natural_row_mode3_build_ns += mode23_res.mode3_elapsed_ns;
            }
            accumulate_mode2_lz(mode23_res.mode2.lz);
            if (stats && mode2.empty()) stats->natural_row_mode2_bias_reject_count++;
        } else {
            if (stats) stats->natural_row_mode12_seq_count++;
            const auto t_mode1_0 = Clock::now();
            mode1 = detail::build_mode1_payload_from_prepared(
                mode1_prepared,
                mode1_pred,
                pad_h,
                pixel_count,
                encode_byte_stream_shared_lz,
                1,
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                }
            );
            const auto t_mode1_1 = Clock::now();
            if (stats) stats->natural_row_mode1_build_ns += ns_since(t_mode1_0, t_mode1_1);
            if (mode2_possible_vs_mode0) {
                const uint64_t best_after_mode1 =
                    std::min<uint64_t>((uint64_t)mode0.size(), (uint64_t)mode1.size());
                const uint64_t mode2_limit_vs_best =
                    (best_after_mode1 * (uint64_t)lz_params.bias_permille) / 1000ull;
                const bool mode2_possible_vs_best =
                    (mode2_min_size <= mode2_limit_vs_best);
                if (mode2_possible_vs_best) {
                    const auto t_mode2_0 = Clock::now();
                    detail::GlobalChainLzCounters lz_counters;
                    mode2 = detail::build_mode1_payload_from_prepared(
                        mode1_prepared,
                        mode1_pred,
                        pad_h,
                        pixel_count,
                        encode_byte_stream_shared_lz,
                        2,
                        [&](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                            return detail::compress_global_chain_lz(bytes, lz_params, &lz_counters);
                        }
                    );
                    const auto t_mode2_1 = Clock::now();
                    if (stats) stats->natural_row_mode2_build_ns += ns_since(t_mode2_0, t_mode2_1);
                    accumulate_mode2_lz(lz_counters);
                    if (stats && mode2.empty()) stats->natural_row_mode2_bias_reject_count++;
                } else if (stats) {
                    stats->natural_row_mode2_bias_reject_count++;
                }
            } else if (stats) {
                stats->natural_row_mode2_bias_reject_count++;
            }
            const auto t_mode3_0 = Clock::now();
            mode3 = detail::build_mode3_payload_from_prepared(
                padded, pad_w, pad_h, pixel_count,
                mode1_prepared,
                mode1_pred,
                encode_byte_stream
            );
            const auto t_mode3_1 = Clock::now();
            if (stats) stats->natural_row_mode3_build_ns += ns_since(t_mode3_0, t_mode3_1);
        }
    }
    if (mode0.empty()) return {};
    if (stats) {
        stats->natural_row_mode0_size_sum += (uint64_t)mode0.size();
        stats->natural_row_mode1_size_sum += (uint64_t)mode1.size();
        stats->natural_row_mode2_size_sum += (uint64_t)mode2.size();
        stats->natural_row_mode3_size_sum += (uint64_t)mode3.size();
    }

    uint8_t selected_mode = 0;
    std::vector<uint8_t> best = std::move(mode0);
    if (!mode1.empty() && mode1.size() < best.size()) {
        best = std::move(mode1);
        selected_mode = 1;
    }
    if (!mode2.empty()) {
        const uint64_t lhs = (uint64_t)mode2.size() * 1000ull;
        const uint64_t rhs = (uint64_t)best.size() * (uint64_t)lz_params.bias_permille;
        if (lhs <= rhs) {
            best = std::move(mode2);
            selected_mode = 2;
            if (stats) stats->natural_row_mode2_bias_adopt_count++;
        } else if (stats) {
            stats->natural_row_mode2_bias_reject_count++;
        }
    }
    if (!mode3.empty() && mode3.size() < best.size()) {
        best = std::move(mode3);
        selected_mode = 3;
    }
    if (stats) {
        if (selected_mode == 0) stats->natural_row_mode0_selected_count++;
        else if (selected_mode == 1) stats->natural_row_mode1_selected_count++;
        else if (selected_mode == 2) stats->natural_row_mode2_selected_count++;
        else stats->natural_row_mode3_selected_count++;
    }
    return best;
}

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn, typename EncodeByteStreamFn>
inline std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
    const int16_t* plane, uint32_t width, uint32_t height,
    ZigzagEncodeFn&& zigzag_encode_val,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    EncodeByteStreamFn&& encode_byte_stream,
    LosslessModeDebugStats* stats = nullptr,
    int mode2_nice_length_override = -1,
    int mode2_match_strategy_override = -1
) {
    if (!plane || width == 0 || height == 0) return {};
    const uint32_t pad_w = ((width + 7) / 8) * 8;
    const uint32_t pad_h = ((height + 7) / 8) * 8;
    const uint32_t pixel_count = pad_w * pad_h;
    if (pixel_count == 0) return {};

    std::vector<int16_t> padded(pixel_count, 0);
    for (uint32_t y = 0; y < pad_h; y++) {
        uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < pad_w; x++) {
            uint32_t sx = std::min(x, width - 1);
            padded[(size_t)y * pad_w + x] = plane[(size_t)sy * width + sx];
        }
    }

    return encode_plane_lossless_natural_row_tile_padded(
        padded.data(), pad_w, pad_h,
        zigzag_encode_val,
        encode_byte_stream_shared_lz,
        encode_byte_stream,
        stats,
        mode2_nice_length_override,
        mode2_match_strategy_override
    );
}

} // namespace hakonyans::lossless_natural_route
