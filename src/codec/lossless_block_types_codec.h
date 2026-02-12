#pragma once

#include "headers.h"
#include "lossless_mode_debug_stats.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_block_types_codec {

inline std::vector<uint8_t> rle_encode_block_types(const std::vector<FileHeader::BlockType>& types) {
    std::vector<uint8_t> raw;
    int current_type = -1;
    int current_run = 0;
    for (auto t : types) {
        int type = (int)t;
        if (type != current_type) {
            if (current_run > 0) {
                while (current_run > 0) {
                    int run = std::min(current_run, 64);
                    raw.push_back((uint8_t)((current_type & 0x03) | ((run - 1) << 2)));
                    current_run -= run;
                }
            }
            current_type = type;
            current_run = 1;
        } else {
            current_run++;
        }
    }
    if (current_run > 0) {
        while (current_run > 0) {
            int run = std::min(current_run, 64);
            raw.push_back((uint8_t)((current_type & 0x03) | ((run - 1) << 2)));
            current_run -= run;
        }
    }
    return raw;
}

template <typename EncodeByteStreamFn, typename CompressLzFn>
inline std::vector<uint8_t> encode_block_types(
    const std::vector<FileHeader::BlockType>& types,
    bool allow_compact,
    EncodeByteStreamFn&& encode_byte_stream,
    CompressLzFn&& compress_lz,
    LosslessModeDebugStats* stats
) {
    auto raw = rle_encode_block_types(types);
    if (!allow_compact) return raw;

    auto mode1_payload = encode_byte_stream(raw);
    auto mode2_payload = compress_lz(raw);

    size_t size_raw = raw.size();
    size_t size_mode1 = 6 + mode1_payload.size();
    size_t size_mode2 = 6 + mode2_payload.size();

    size_t best_size = size_raw;
    int best_mode = 0;

    if (size_mode1 < best_size && size_mode1 * 100 <= size_raw * 98) {
        best_size = size_mode1;
        best_mode = 1;
    }
    if (size_mode2 < best_size && size_mode2 * 100 <= size_raw * 98) {
        best_size = size_mode2;
        best_mode = 2;
    }

    if (best_mode == 1) {
        std::vector<uint8_t> out;
        out.resize(6);
        out[0] = FileHeader::WRAPPER_MAGIC_BLOCK_TYPES;
        out[1] = 1;
        uint32_t rc = (uint32_t)size_raw;
        std::memcpy(&out[2], &rc, 4);
        out.insert(out.end(), mode1_payload.begin(), mode1_payload.end());
        return out;
    }

    if (best_mode == 2) {
        std::vector<uint8_t> out;
        out.resize(6);
        out[0] = FileHeader::WRAPPER_MAGIC_BLOCK_TYPES;
        out[1] = 2;
        uint32_t rc = (uint32_t)size_raw;
        std::memcpy(&out[2], &rc, 4);
        out.insert(out.end(), mode2_payload.begin(), mode2_payload.end());

        if (stats) {
            stats->block_types_lz_used_count++;
            stats->block_types_lz_saved_bytes_sum += (size_raw - out.size());
        }

        return out;
    }

    return raw;
}

template <typename DecodeByteStreamFn, typename DecompressLzFn>
inline std::vector<FileHeader::BlockType> decode_block_types(
    const uint8_t* val,
    size_t sz,
    int nb,
    uint16_t file_version,
    DecodeByteStreamFn&& decode_byte_stream,
    DecompressLzFn&& decompress_lz
) {
    std::vector<FileHeader::BlockType> out;
    out.reserve(nb);

    const uint8_t* runs = val;
    size_t runs_size = sz;
    std::vector<uint8_t> decoded_runs;

    if (file_version >= FileHeader::VERSION_BLOCK_TYPES_V2 && sz >= 6) {
        if (val[0] == FileHeader::WRAPPER_MAGIC_BLOCK_TYPES) {
            uint8_t mode = val[1];
            uint32_t raw_count = 0;
            std::memcpy(&raw_count, val + 2, 4);
            const uint8_t* enc_ptr = val + 6;
            size_t enc_size = sz - 6;

            if (mode == 1) {
                decoded_runs = decode_byte_stream(enc_ptr, enc_size, raw_count);
            } else if (mode == 2) {
                decoded_runs = decompress_lz(enc_ptr, enc_size, raw_count);
            }

            if (!decoded_runs.empty()) {
                runs = decoded_runs.data();
                runs_size = decoded_runs.size();
            }
        }
    }

    for (size_t i = 0; i < runs_size; i++) {
        uint8_t v = runs[i];
        uint8_t type = v & 0x03;
        int run = ((v >> 2) & 0x3F) + 1;
        for (int k = 0; k < run; k++) {
            if (out.size() < (size_t)nb) {
                out.push_back((FileHeader::BlockType)type);
            }
        }
    }

    if (out.size() < (size_t)nb) {
        out.resize(nb, FileHeader::BlockType::DCT);
    }
    return out;
}

} // namespace hakonyans::lossless_block_types_codec
