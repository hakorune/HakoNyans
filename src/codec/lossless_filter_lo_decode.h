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
            // Mode 6: v0x0017 (type bitpack + len split), v0x0016 (compact dist), or v0x0015 (legacy)
            // v0x0017: type_bits + lit_len + match_len + dist + lit, payload min = 36 bytes
            // v0x0016: type[] + len[] + dist[] (MATCH only) + lit, payload min = 28 bytes
            // v0x0015: type[] + len[] + dist[] (all tokens) + lit, payload min = 24 bytes
            const bool is_v17 = (file_version >= FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS_V3);
            const bool is_v16 = (!is_v17 && file_version >= FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS_V2);
            const size_t min_payload_size = is_v17 ? 36 : (is_v16 ? 28 : 24);

            if (payload_size < min_payload_size) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                if (stats) stats->plane_filter_lo_mode6_count++;

                uint32_t token_count = (uint32_t)payload[0]
                                     | ((uint32_t)payload[1] << 8)
                                     | ((uint32_t)payload[2] << 16)
                                     | ((uint32_t)payload[3] << 24);
                uint32_t match_count = 0;
                uint32_t lit_token_count = 0;
                bool is_v17_valid = false;

                size_t header_offset = 4; // After token_count
                if (is_v17) {
                    match_count = (uint32_t)payload[4]
                                | ((uint32_t)payload[5] << 8)
                                | ((uint32_t)payload[6] << 16)
                                | ((uint32_t)payload[7] << 24);
                    lit_token_count = (uint32_t)payload[8]
                                    | ((uint32_t)payload[9] << 8)
                                    | ((uint32_t)payload[10] << 16)
                                    | ((uint32_t)payload[11] << 24);
                    header_offset = 12; // After token_count + match_count + lit_token_count

                    // Pre-check: token_count == lit_token_count + match_count
                    if (token_count != lit_token_count + match_count) {
                        lo_bytes.assign(raw_count, 0);
                        if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                    } else {
                        is_v17_valid = true;
                    }
                } else if (is_v16) {
                    match_count = (uint32_t)payload[4]
                                | ((uint32_t)payload[5] << 8)
                                | ((uint32_t)payload[6] << 16)
                                | ((uint32_t)payload[7] << 24);
                    header_offset = 8; // After token_count + match_count
                }

                // Parse stream sizes based on version
                uint32_t type_sz, lit_len_sz = 0, match_len_sz = 0, dist_lo_sz, dist_hi_sz, lit_sz;

                if (is_v17) {
                    type_sz = (uint32_t)payload[header_offset]
                            | ((uint32_t)payload[header_offset + 1] << 8)
                            | ((uint32_t)payload[header_offset + 2] << 16)
                            | ((uint32_t)payload[header_offset + 3] << 24);
                    lit_len_sz = (uint32_t)payload[header_offset + 4]
                               | ((uint32_t)payload[header_offset + 5] << 8)
                               | ((uint32_t)payload[header_offset + 6] << 16)
                               | ((uint32_t)payload[header_offset + 7] << 24);
                    match_len_sz = (uint32_t)payload[header_offset + 8]
                                 | ((uint32_t)payload[header_offset + 9] << 8)
                                 | ((uint32_t)payload[header_offset + 10] << 16)
                                 | ((uint32_t)payload[header_offset + 11] << 24);
                    dist_lo_sz = (uint32_t)payload[header_offset + 12]
                               | ((uint32_t)payload[header_offset + 13] << 8)
                               | ((uint32_t)payload[header_offset + 14] << 16)
                               | ((uint32_t)payload[header_offset + 15] << 24);
                    dist_hi_sz = (uint32_t)payload[header_offset + 16]
                               | ((uint32_t)payload[header_offset + 17] << 8)
                               | ((uint32_t)payload[header_offset + 18] << 16)
                               | ((uint32_t)payload[header_offset + 19] << 24);
                    lit_sz = (uint32_t)payload[header_offset + 20]
                           | ((uint32_t)payload[header_offset + 21] << 8)
                           | ((uint32_t)payload[header_offset + 22] << 16)
                           | ((uint32_t)payload[header_offset + 23] << 24);
                } else {
                    // v0x0015 and v0x0016 share same stream layout
                    type_sz = (uint32_t)payload[header_offset]
                            | ((uint32_t)payload[header_offset + 1] << 8)
                            | ((uint32_t)payload[header_offset + 2] << 16)
                            | ((uint32_t)payload[header_offset + 3] << 24);
                    uint32_t len_sz = (uint32_t)payload[header_offset + 4]
                                    | ((uint32_t)payload[header_offset + 5] << 8)
                                    | ((uint32_t)payload[header_offset + 6] << 16)
                                    | ((uint32_t)payload[header_offset + 7] << 24);
                    lit_len_sz = len_sz; // For v15/v16, use common len stream
                    dist_lo_sz = (uint32_t)payload[header_offset + 8]
                               | ((uint32_t)payload[header_offset + 9] << 8)
                               | ((uint32_t)payload[header_offset + 10] << 16)
                               | ((uint32_t)payload[header_offset + 11] << 24);
                    dist_hi_sz = (uint32_t)payload[header_offset + 12]
                               | ((uint32_t)payload[header_offset + 13] << 8)
                               | ((uint32_t)payload[header_offset + 14] << 16)
                               | ((uint32_t)payload[header_offset + 15] << 24);
                    lit_sz = (uint32_t)payload[header_offset + 16]
                           | ((uint32_t)payload[header_offset + 17] << 8)
                           | ((uint32_t)payload[header_offset + 18] << 16)
                           | ((uint32_t)payload[header_offset + 19] << 24);
                }

                const size_t total_header = is_v17 ? (header_offset + 24) : (header_offset + 20);
                bool sizes_ok = (payload_size >= total_header);
                size_t remain = sizes_ok ? (payload_size - total_header) : 0;
                auto consume = [&](uint32_t sz) {
                    if (!sizes_ok) return;
                    if ((size_t)sz > remain) {
                        sizes_ok = false;
                        return;
                    }
                    remain -= (size_t)sz;
                };
                consume(type_sz);
                if (is_v17) {
                    consume(lit_len_sz);
                    consume(match_len_sz);
                } else {
                    consume(lit_len_sz); // len_sz for v15/v16
                }
                consume(dist_lo_sz);
                consume(dist_hi_sz);
                consume(lit_sz);
                if (sizes_ok && remain != 0) sizes_ok = false;

                if (is_v17 && !is_v17_valid) {
                    // v17 pre-check failed, already zero-filled above
                } else if (!sizes_ok) {
                    lo_bytes.assign(raw_count, 0);
                    if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                } else {
                    const uint8_t* type_ptr = payload + total_header;
                    const uint8_t* lit_len_ptr = type_ptr + type_sz;
                    const uint8_t* match_len_ptr = is_v17 ? (lit_len_ptr + lit_len_sz) : nullptr;
                    const uint8_t* dist_lo_ptr = is_v17 ? (match_len_ptr + match_len_sz) : (lit_len_ptr + lit_len_sz);
                    const uint8_t* dist_hi_ptr = dist_lo_ptr + dist_lo_sz;
                    const uint8_t* lit_ptr = dist_hi_ptr + dist_hi_sz;

                    // Decode streams
                    std::vector<uint8_t> type_stream, lit_len_stream, match_len_stream, dist_lo_stream, dist_hi_stream, lit_stream;
                    size_t expected_type_size = is_v17 ? ((token_count + 7) / 8) : token_count;
                    size_t expected_dist_size = is_v17 ? match_count : (is_v16 ? match_count : token_count);

                    if (use_shared_lz_cdf) {
                        if (stats) stats->plane_filter_lo_mode6_shared_cdf_count++;
                        type_stream = timed_decode_shared_rans(type_ptr, type_sz, expected_type_size);
                        if (is_v17) {
                            lit_len_stream = timed_decode_shared_rans(lit_len_ptr, lit_len_sz, lit_token_count);
                            match_len_stream = timed_decode_shared_rans(match_len_ptr, match_len_sz, match_count);
                        } else {
                            lit_len_stream = timed_decode_shared_rans(lit_len_ptr, lit_len_sz, token_count);
                        }
                        dist_lo_stream = timed_decode_shared_rans(dist_lo_ptr, dist_lo_sz, expected_dist_size);
                        dist_hi_stream = timed_decode_shared_rans(dist_hi_ptr, dist_hi_sz, expected_dist_size);
                        lit_stream = timed_decode_shared_rans(lit_ptr, lit_sz, 0);
                    } else {
                        if (stats) stats->plane_filter_lo_mode6_legacy_cdf_count++;
                        type_stream = timed_decode_rans(type_ptr, type_sz, expected_type_size);
                        if (is_v17) {
                            lit_len_stream = timed_decode_rans(lit_len_ptr, lit_len_sz, lit_token_count);
                            match_len_stream = timed_decode_rans(match_len_ptr, match_len_sz, match_count);
                        } else {
                            lit_len_stream = timed_decode_rans(lit_len_ptr, lit_len_sz, token_count);
                        }
                        dist_lo_stream = timed_decode_rans(dist_lo_ptr, dist_lo_sz, expected_dist_size);
                        dist_hi_stream = timed_decode_rans(dist_hi_ptr, dist_hi_sz, expected_dist_size);
                        lit_stream = timed_decode_rans(lit_ptr, lit_sz, 0);
                    }

                    // Strict stream length verification
                    bool stream_lengths_ok;
                    if (is_v17) {
                        stream_lengths_ok =
                            (type_stream.size() == expected_type_size) &&
                            (lit_len_stream.size() == lit_token_count) &&
                            (match_len_stream.size() == match_count) &&
                            (dist_lo_stream.size() == match_count) &&
                            (dist_hi_stream.size() == match_count);
                    } else {
                        stream_lengths_ok =
                            (type_stream.size() == token_count) &&
                            (lit_len_stream.size() == token_count) &&
                            (dist_lo_stream.size() == expected_dist_size) &&
                            (dist_hi_stream.size() == expected_dist_size);
                    }

                    if (!stream_lengths_ok) {
                        lo_bytes.assign(raw_count, 0);
                        if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                    } else {
                        // Reconstruct TileLZ byte stream from tokens
                        std::vector<uint8_t> lz_payload;
                        lz_payload.reserve(token_count * 4 + lit_stream.size());
                        size_t lit_pos = 0;
                        size_t dist_pos = 0;
                        size_t lit_len_pos = 0;
                        size_t match_len_pos = 0;
                        bool reconstruct_ok = true;

                        for (size_t i = 0; i < token_count && reconstruct_ok; i++) {
                            uint8_t type;
                            uint8_t len;

                            if (is_v17) {
                                // Extract type bit from packed bytes
                                size_t byte_idx = i / 8;
                                size_t bit_pos = i % 8;
                                type = (type_stream[byte_idx] >> bit_pos) & 1;

                                if (type == 0) { // LITRUN
                                    if (lit_len_pos >= lit_len_stream.size()) {
                                        reconstruct_ok = false;
                                        break;
                                    }
                                    len = lit_len_stream[lit_len_pos++];
                                } else { // MATCH
                                    if (match_len_pos >= match_len_stream.size()) {
                                        reconstruct_ok = false;
                                        break;
                                    }
                                    len = match_len_stream[match_len_pos++];
                                }
                            } else {
                                // v0x0015 and v0x0016 use separate type/len arrays
                                type = type_stream[i];
                                len = lit_len_stream[i];
                            }

                            if (type == 0) { // LITRUN
                                lz_payload.push_back(0);
                                lz_payload.push_back(len);
                                if (lit_pos + len > lit_stream.size()) {
                                    reconstruct_ok = false;
                                    break;
                                }
                                lz_payload.insert(lz_payload.end(), lit_stream.data() + lit_pos, lit_stream.data() + lit_pos + len);
                                lit_pos += len;
                            } else if (type == 1) { // MATCH
                                if (dist_pos >= dist_lo_stream.size() || dist_pos >= dist_hi_stream.size()) {
                                    reconstruct_ok = false;
                                    break;
                                }
                                uint8_t dlo = dist_lo_stream[dist_pos];
                                uint8_t dhi = dist_hi_stream[dist_pos];
                                dist_pos++;
                                lz_payload.push_back(1);
                                lz_payload.push_back(len);
                                lz_payload.push_back(dlo);
                                lz_payload.push_back(dhi);
                            } else {
                                reconstruct_ok = false;
                            }
                        }

                        // Final verification for v17
                        if (is_v17) {
                            reconstruct_ok = reconstruct_ok &&
                                (lit_pos == lit_stream.size()) &&
                                (dist_pos == match_count) &&
                                (lit_len_pos == lit_token_count) &&
                                (match_len_pos == match_count);
                        } else {
                            reconstruct_ok = reconstruct_ok &&
                                (lit_pos == lit_stream.size()) &&
                                (dist_pos == (is_v16 ? match_count : token_count));
                        }

                        // Final verification: all streams must be fully consumed
                        if (reconstruct_ok &&
                            lit_pos == lit_stream.size() &&
                            dist_pos == expected_dist_size &&
                            !lz_payload.empty()) {
                            lo_bytes = timed_decompress_lz(lz_payload.data(), lz_payload.size(), raw_count);
                            if (lo_bytes.size() != raw_count) {
                                lo_bytes.assign(raw_count, 0);
                                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                            }
                        } else {
                            lo_bytes.assign(raw_count, 0);
                            if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                        }
                    }
                }
            }
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
            if (stats) stats->plane_filter_lo_mode7_count++;
            const uint32_t shared_mask = (uint32_t)payload[0]
                                       | ((uint32_t)payload[1] << 8)
                                       | ((uint32_t)payload[2] << 16)
                                       | ((uint32_t)payload[3] << 24);
            uint32_t lens[6] = {0, 0, 0, 0, 0, 0};
            for (int k = 0; k < 6; k++) {
                const size_t pos = 4 + (size_t)k * 4;
                lens[k] = (uint32_t)payload[pos]
                        | ((uint32_t)payload[pos + 1] << 8)
                        | ((uint32_t)payload[pos + 2] << 16)
                        | ((uint32_t)payload[pos + 3] << 24);
            }
            size_t off = 28;
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

                std::vector<std::vector<uint8_t>> ctx_decoded(6);
                size_t ctx_offsets[6] = {0, 0, 0, 0, 0, 0};
                off = 28;
                for (int k = 0; k < 6; k++) {
                    ctx_offsets[k] = off;
                    off += lens[k];
                }

                uint64_t shared_ctx_used = 0;
                for (int k = 0; k < 6; k++) {
                    if (ctx_expected[k] == 0 || lens[k] == 0) continue;
                    if ((shared_mask >> k) & 1u) shared_ctx_used++;
                }
                if (stats) stats->plane_filter_lo_mode7_shared_ctx_sum += shared_ctx_used;

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
                        bool used_shared = false;
                    };
                    std::vector<std::future<CtxDecodeResult>> futs(6);
                    std::vector<bool> launched(6, false);
                    for (int k = 0; k < 6; k++) {
                        if (lens[k] == 0) continue;
                        launched[k] = true;
                        const bool use_shared_ctx = ((shared_mask >> k) & 1u) != 0u;
                        futs[k] = std::async(std::launch::async, [&, k, use_shared_ctx]() {
                            thread_budget::ScopedParallelRegion guard;
                            const auto t_ctx0 = Clock::now();
                            std::vector<uint8_t> out;
                            if (use_shared_ctx) {
                                out = decode_byte_stream_shared_lz(
                                    payload + ctx_offsets[k], lens[k], ctx_expected[k]
                                );
                            } else {
                                out = decode_byte_stream(
                                    payload + ctx_offsets[k], lens[k], ctx_expected[k]
                                );
                            }
                            const auto t_ctx1 = Clock::now();
                            CtxDecodeResult r;
                            r.bytes = std::move(out);
                            r.elapsed_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                t_ctx1 - t_ctx0
                            ).count();
                            r.used_shared = use_shared_ctx;
                            return r;
                        });
                    }
                    for (int k = 0; k < 6; k++) {
                        if (!launched[k]) continue;
                        auto r = futs[k].get();
                        ctx_decoded[k] = std::move(r.bytes);
                        if (stats) {
                            if (r.used_shared) stats->plane_filter_lo_decode_shared_rans_ns += r.elapsed_ns;
                            else stats->plane_filter_lo_decode_rans_ns += r.elapsed_ns;
                        }
                    }
                } else {
                    for (int k = 0; k < 6; k++) {
                        if (lens[k] == 0) continue;
                        const bool use_shared_ctx = ((shared_mask >> k) & 1u) != 0u;
                        if (use_shared_ctx) {
                            ctx_decoded[k] = timed_decode_shared_rans(
                                payload + ctx_offsets[k], lens[k], ctx_expected[k]
                            );
                        } else {
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
        } else if (lo_mode == 8 && file_version >= FileHeader::VERSION_FILTER_LO_CTX_HYBRID_CODEC && payload_size >= 30) {
            if (stats) stats->plane_filter_lo_mode8_count++;

            // Read ctx_codec_id[6] from payload[0..5]
            uint8_t codec_ids[6];
            for (int k = 0; k < 6; k++) {
                codec_ids[k] = payload[k];
            }

            // Read lens[6] from payload[6..29]
            uint32_t lens[6] = {0, 0, 0, 0, 0, 0};
            for (int k = 0; k < 6; k++) {
                const size_t pos = 6 + (size_t)k * 4;
                lens[k] = (uint32_t)payload[pos]
                        | ((uint32_t)payload[pos + 1] << 8)
                        | ((uint32_t)payload[pos + 2] << 16)
                        | ((uint32_t)payload[pos + 3] << 24);
            }

            // Validate payload_size == 30 + sum(lens)
            size_t total_stream_size = 30;
            for (int k = 0; k < 6; k++) {
                total_stream_size += lens[k];
            }

            // Validate codec_ids and lens
            bool validation_ok = (payload_size == total_stream_size);
            for (int k = 0; k < 6 && validation_ok; k++) {
                if (codec_ids[k] != 0 && codec_ids[k] != 1 && codec_ids[k] != 2 && codec_ids[k] != 255) {
                    validation_ok = false;  // Invalid codec_id
                }
                if (codec_ids[k] == 255 && lens[k] != 0) {
                    validation_ok = false;  // Empty ctx must have len=0
                }
            }

            if (!validation_ok) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                const auto t_mode8_rows0 = Clock::now();
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
                const auto t_mode8_rows1 = Clock::now();
                add_ns(stats ? &stats->plane_filter_lo_mode4_row_lens_ns : nullptr, t_mode8_rows0, t_mode8_rows1);

                std::vector<std::vector<uint8_t>> ctx_decoded(6);
                size_t ctx_offsets[6] = {0, 0, 0, 0, 0, 0};
                size_t off = 30;
                for (int k = 0; k < 6; k++) {
                    ctx_offsets[k] = off;
                    off += lens[k];
                }

                // Decode each context based on codec_id
                bool decode_ok = true;
                for (int k = 0; k < 6 && decode_ok; k++) {
                    if (ctx_expected[k] == 0 || lens[k] == 0) continue;

                    std::vector<uint8_t> decoded;
                    if (codec_ids[k] == 0) {
                        // Legacy rANS
                        decoded = timed_decode_rans(payload + ctx_offsets[k], lens[k], ctx_expected[k]);
                    } else if (codec_ids[k] == 1) {
                        // Delta + rANS: decode then cumulative sum
                        auto delta = timed_decode_rans(payload + ctx_offsets[k], lens[k], ctx_expected[k]);
                        decoded.resize(delta.size());
                        if (!delta.empty()) {
                            decoded[0] = delta[0];
                            for (size_t i = 1; i < delta.size(); i++) {
                                decoded[i] = (uint8_t)(decoded[i - 1] + delta[i]);
                            }
                        }
                    } else if (codec_ids[k] == 2) {
                        // LZ + rANS(shared): decode shared then decompress
                        auto lz_payload = timed_decode_shared_rans(payload + ctx_offsets[k], lens[k], 0);
                        if (!lz_payload.empty()) {
                            decoded = timed_decompress_lz(lz_payload.data(), lz_payload.size(), ctx_expected[k]);
                        }
                    }

                    // Validate decoded size
                    if (decoded.size() != ctx_expected[k]) {
                        decode_ok = false;
                    } else {
                        ctx_decoded[k] = std::move(decoded);
                    }
                }

                if (!decode_ok) {
                    lo_bytes.assign(raw_count, 0);
                    if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                } else {
                    // Reassemble rows
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
            }
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
