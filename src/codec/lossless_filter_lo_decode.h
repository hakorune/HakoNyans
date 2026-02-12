#pragma once

#include "headers.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
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
    DecodeByteStreamFn&& decode_byte_stream,
    DecodeByteStreamSharedLzFn&& decode_byte_stream_shared_lz,
    DecompressLzFn&& decompress_lz
) {
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
            auto delta = decode_byte_stream(payload, payload_size, raw_count);
            lo_bytes.resize(raw_count);
            if (!delta.empty()) {
                lo_bytes[0] = delta[0];
                for (size_t i = 1; i < raw_count && i < delta.size(); i++) {
                    lo_bytes[i] = (uint8_t)(lo_bytes[i - 1] + delta[i]);
                }
            }
        } else if (lo_mode == 2) {
            lo_bytes = decompress_lz(payload, payload_size, raw_count);
        } else if (lo_mode == 5) {
            std::vector<uint8_t> lz_payload;
            if (use_shared_lz_cdf) {
                lz_payload = decode_byte_stream_shared_lz(payload, payload_size, 0);
            } else {
                lz_payload = decode_byte_stream(payload, payload_size, 0);
            }
            if (!lz_payload.empty()) {
                lo_bytes = decompress_lz(lz_payload.data(), lz_payload.size(), raw_count);
            } else {
                lo_bytes.assign(raw_count, 0);
            }
        } else if (lo_mode == 3 && payload_size >= 4) {
            uint32_t pred_sz = (uint32_t)payload[0]
                             | ((uint32_t)payload[1] << 8)
                             | ((uint32_t)payload[2] << 16)
                             | ((uint32_t)payload[3] << 24);
            if ((size_t)pred_sz + 4 > payload_size) {
                lo_bytes.assign(raw_count, 0);
            } else {
                const uint8_t* pred_ptr = payload + 4;
                const uint8_t* resid_ptr = payload + 4 + pred_sz;
                size_t resid_sz = payload_size - 4 - pred_sz;

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

                auto preds = decode_byte_stream(pred_ptr, pred_sz, active_rows);
                auto resids = decode_byte_stream(resid_ptr, resid_sz, raw_count);

                lo_bytes.reserve(raw_count);
                size_t resid_idx = 0;
                size_t pred_idx = 0;
                size_t prev_valid_row_start = 0;
                size_t prev_valid_row_len = 0;

                for (uint32_t y = 0; y < pad_h; y++) {
                    int len = row_lens[y];
                    if (len == 0) continue;

                    int p = (pred_idx < preds.size()) ? preds[pred_idx++] : 0;
                    size_t start_idx = lo_bytes.size();

                    for (int i = 0; i < len; i++) {
                        uint8_t resid = (resid_idx < resids.size()) ? resids[resid_idx++] : 0;
                        uint8_t pred_val = 0;
                        if (p == 1) {
                            pred_val = (i == 0) ? 0 : lo_bytes[start_idx + i - 1];
                        } else if (p == 2) {
                            pred_val = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                        } else if (p == 3) {
                            uint8_t left = (i == 0) ? 0 : lo_bytes[start_idx + i - 1];
                            uint8_t up = (prev_valid_row_len > (size_t)i) ? lo_bytes[prev_valid_row_start + i] : 0;
                            pred_val = (left + up) / 2;
                        }
                        lo_bytes.push_back((uint8_t)(resid + pred_val));
                    }
                    prev_valid_row_start = start_idx;
                    prev_valid_row_len = len;
                }
            }
        } else if (lo_mode == 4 && payload_size >= 24) {
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
            } else {
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

                std::vector<std::vector<uint8_t>> ctx_decoded(6);
                off = 24;
                for (int k = 0; k < 6; k++) {
                    if (lens[k] > 0) {
                        ctx_decoded[k] = decode_byte_stream(payload + off, lens[k], ctx_expected[k]);
                    } else {
                        ctx_decoded[k].clear();
                    }
                    off += lens[k];
                }

                std::vector<size_t> ctx_pos(6, 0);
                lo_bytes.clear();
                lo_bytes.reserve(raw_count);
                for (uint32_t y = 0; y < pad_h && lo_bytes.size() < raw_count; y++) {
                    int len = row_lens[y];
                    if (len <= 0) continue;
                    uint8_t fid = (y < filter_ids.size()) ? filter_ids[y] : 0;
                    if (fid > 5) fid = 0;
                    for (int i = 0; i < len && lo_bytes.size() < raw_count; i++) {
                        if (ctx_pos[fid] < ctx_decoded[fid].size()) {
                            lo_bytes.push_back(ctx_decoded[fid][ctx_pos[fid]++]);
                        } else {
                            lo_bytes.push_back(0);
                        }
                    }
                }
            }
        }

        if (lo_bytes.size() < filter_pixel_count) {
            lo_bytes.resize(filter_pixel_count, 0);
        }
    } else {
        lo_bytes = decode_byte_stream(ptr_lo, lo_stream_size, filter_pixel_count);
    }

    return lo_bytes;
}

} // namespace hakonyans::lossless_filter_lo_decode
