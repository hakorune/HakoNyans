#pragma once

#include "copy.h"
#include "headers.h"
#include "lossless_block_types_codec.h"
#include "lossless_filter.h"
#include "lossless_filter_lo_decode.h"
#include "lossless_natural_decode.h"
#include "lossless_decode_debug_stats.h"
#include "lossless_tile4_codec.h"
#include "lz_tile.h"
#include "palette.h"
#include "zigzag.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossless_plane_decode_core {

template <typename DecodeByteStreamFn, typename DecodeSharedLzFn>
inline std::vector<int16_t> decode_plane_lossless(
    const uint8_t* td,
    size_t ts,
    uint32_t width,
    uint32_t height,
    uint16_t file_version,
    DecodeByteStreamFn&& decode_byte_stream,
    DecodeSharedLzFn&& decode_byte_stream_shared_lz,
    ::hakonyans::LosslessDecodeDebugStats* perf_stats = nullptr
) {
    using Clock = std::chrono::steady_clock;
    auto add_ns = [&](uint64_t* dst, const Clock::time_point& t0, const Clock::time_point& t1) {
        if (!dst) return;
        *dst += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };

    uint32_t pad_w = ((width + 7) / 8) * 8;
    uint32_t pad_h = ((height + 7) / 8) * 8;
    int nx = (int)(pad_w / 8), ny = (int)(pad_h / 8), nb = nx * ny;

    std::vector<int16_t> natural_decoded;
    const auto t_nat0 = Clock::now();
    if (lossless_natural_decode::try_decode_natural_row_wrapper(
            td,
            ts,
            width,
            height,
            pad_w,
            pad_h,
            file_version,
            [&](const uint8_t* data, size_t size, size_t raw_count) {
                return decode_byte_stream_shared_lz(data, size, raw_count);
            },
            [&](const uint8_t* data, size_t size, size_t raw_count) {
                return decode_byte_stream(data, size, raw_count);
            },
            natural_decoded)) {
        const auto t_nat1 = Clock::now();
        add_ns(perf_stats ? &perf_stats->plane_try_natural_ns : nullptr, t_nat0, t_nat1);
        return natural_decoded;
    }
    const auto t_nat1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_try_natural_ns : nullptr, t_nat0, t_nat1);

    const auto t_screen0 = Clock::now();
    if (ts >= 14 &&
        file_version >= FileHeader::VERSION_SCREEN_INDEXED_TILE &&
        td[0] == FileHeader::WRAPPER_MAGIC_SCREEN_INDEXED) {
        auto read_u16 = [](const uint8_t* p) -> uint16_t {
            return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        };
        auto read_u32 = [](const uint8_t* p) -> uint32_t {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        };

        uint8_t mode = td[1];
        uint8_t bits = td[2];
        uint16_t palette_count = read_u16(td + 4);
        uint32_t pixel_count = read_u32(td + 6);
        uint32_t raw_packed_size = read_u32(td + 10);
        uint32_t expected_pixels = pad_w * pad_h;

        std::vector<int16_t> zeros(width * height, 0);
        if (pixel_count != expected_pixels) return zeros;
        if (palette_count == 0 || bits > 7) return zeros;

        size_t pos = 14;
        size_t palette_bytes = (size_t)palette_count * 2ull;
        if (pos + palette_bytes > ts) return zeros;

        std::vector<int16_t> palette_vals(palette_count, 0);
        for (uint16_t i = 0; i < palette_count; i++) {
            uint16_t uv = read_u16(td + pos + (size_t)i * 2ull);
            palette_vals[i] = (int16_t)uv;
        }
        pos += palette_bytes;

        const uint8_t* payload = td + pos;
        size_t payload_size = ts - pos;
        std::vector<uint8_t> packed;

        if (bits == 0 || raw_packed_size == 0) {
            raw_packed_size = 0;
        } else if (mode == 0) {
            if (payload_size < raw_packed_size) return zeros;
            packed.assign(payload, payload + raw_packed_size);
        } else if (mode == 1) {
            packed = decode_byte_stream(payload, payload_size, raw_packed_size);
            if (packed.size() < raw_packed_size) return zeros;
        } else if (mode == 2) {
            packed = TileLZ::decompress(payload, payload_size, raw_packed_size);
            if (packed.size() < raw_packed_size) return zeros;
        } else {
            return zeros;
        }

        std::vector<int16_t> padded(pixel_count, palette_vals[0]);
        if (bits > 0 && raw_packed_size > 0) {
            uint64_t acc = 0;
            int acc_bits = 0;
            size_t byte_pos = 0;
            const uint32_t mask = (1u << bits) - 1u;
            for (uint32_t i = 0; i < pixel_count; i++) {
                while (acc_bits < bits) {
                    if (byte_pos >= packed.size()) return zeros;
                    acc |= ((uint64_t)packed[byte_pos++]) << acc_bits;
                    acc_bits += 8;
                }
                uint32_t idx = (uint32_t)(acc & mask);
                acc >>= bits;
                acc_bits -= bits;
                if (idx >= palette_vals.size()) idx = 0;
                padded[i] = palette_vals[idx];
            }
        }

        std::vector<int16_t> result(width * height, 0);
        for (uint32_t y = 0; y < height; y++) {
            std::memcpy(&result[y * width], &padded[y * pad_w], width * sizeof(int16_t));
        }
        const auto t_screen1 = Clock::now();
        add_ns(perf_stats ? &perf_stats->plane_screen_wrapper_ns : nullptr, t_screen0, t_screen1);
        return result;
    }
    const auto t_screen1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_screen_wrapper_ns : nullptr, t_screen0, t_screen1);

    uint32_t hdr[8];
    std::memcpy(hdr, td, 32);
    uint32_t filter_ids_size = hdr[0];
    uint32_t lo_stream_size = hdr[1];
    uint32_t hi_stream_size = hdr[2];
    uint32_t filter_pixel_count = hdr[3];
    uint32_t block_types_size = hdr[4];
    uint32_t palette_data_size = hdr[5];
    uint32_t copy_data_size = hdr[6];
    uint32_t tile4_data_size = (file_version >= FileHeader::VERSION_TILE_MATCH4) ? hdr[7] : 0;

    const uint8_t* ptr = td + 32;
    const uint8_t* ptr_filter_ids = ptr;
    const uint8_t* ptr_lo = ptr_filter_ids + filter_ids_size;
    const uint8_t* ptr_hi = ptr_lo + lo_stream_size;
    const uint8_t* ptr_bt = ptr_hi + hi_stream_size;

    const auto t_bt0 = Clock::now();
    std::vector<FileHeader::BlockType> block_types;
    if (block_types_size > 0) {
        block_types = lossless_block_types_codec::decode_block_types(
            ptr_bt,
            block_types_size,
            nb,
            file_version,
            [&](const uint8_t* data, size_t size, size_t raw_count) {
                return decode_byte_stream(data, size, raw_count);
            },
            [&](const uint8_t* data, size_t size, size_t raw_count) {
                return TileLZ::decompress(data, size, raw_count);
            }
        );
    } else {
        block_types.assign(nb, FileHeader::BlockType::DCT);
    }
    const auto t_bt1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_block_types_ns : nullptr, t_bt0, t_bt1);

    const auto t_fid0 = Clock::now();
    std::vector<uint8_t> filter_ids;
    if (filter_ids_size > 0 && ptr_filter_ids[0] == FileHeader::WRAPPER_MAGIC_FILTER_IDS &&
        filter_ids_size >= 3) {
        uint8_t fid_mode = ptr_filter_ids[1];
        const uint8_t* fid_data = ptr_filter_ids + 2;
        size_t fid_data_size = filter_ids_size - 2;
        if (fid_mode == 1) {
            filter_ids = decode_byte_stream(fid_data, fid_data_size, pad_h);
        } else if (fid_mode == 2) {
            filter_ids = TileLZ::decompress(fid_data, fid_data_size, pad_h);
        }
        if (filter_ids.size() < pad_h) filter_ids.resize(pad_h, 0);
    } else {
        filter_ids.assign(ptr_filter_ids, ptr_filter_ids + filter_ids_size);
    }
    const auto t_fid1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_filter_ids_ns : nullptr, t_fid0, t_fid1);
    ptr += filter_ids_size;

    const auto t_lo0 = Clock::now();
    std::vector<uint8_t> lo_bytes = lossless_filter_lo_decode::decode_filter_lo_stream(
        ptr_lo,
        lo_stream_size,
        filter_pixel_count,
        filter_ids,
        block_types,
        pad_h,
        nx,
        file_version >= FileHeader::VERSION_FILTER_LO_LZ_RANS_SHARED_CDF,
        file_version >= FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS,
        [&](const uint8_t* data, size_t size, size_t raw_count) {
            return decode_byte_stream(data, size, raw_count);
        },
        [&](const uint8_t* data, size_t size, size_t raw_count) {
            return decode_byte_stream_shared_lz(data, size, raw_count);
        },
        [&](const uint8_t* data, size_t size, size_t raw_count) {
            return TileLZ::decompress(data, size, raw_count);
        },
        perf_stats
    );
    const auto t_lo1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_filter_lo_ns : nullptr, t_lo0, t_lo1);
    ptr += lo_stream_size;

    const auto t_hi0 = Clock::now();
    std::vector<uint8_t> hi_bytes;
    if (hi_stream_size > 0 && filter_pixel_count > 0) {
        if (hi_stream_size >= 4 && ptr[0] == FileHeader::WRAPPER_MAGIC_FILTER_HI) {
            uint32_t nz_count = (uint32_t)ptr[1] | ((uint32_t)ptr[2] << 8) |
                                ((uint32_t)ptr[3] << 16);
            size_t mask_size = ((size_t)filter_pixel_count + 7) / 8;
            const uint8_t* mask_ptr = ptr + 4;
            const uint8_t* nz_rans_ptr = mask_ptr + mask_size;
            size_t nz_rans_size =
                (hi_stream_size > 4 + mask_size) ? (hi_stream_size - 4 - mask_size) : 0;

            std::vector<uint8_t> nz_vals;
            if (nz_count > 0 && nz_rans_size > 0) {
                nz_vals = decode_byte_stream(nz_rans_ptr, nz_rans_size, nz_count);
            }

            hi_bytes.resize(filter_pixel_count, 0);
            size_t nz_idx = 0;
            for (size_t i = 0; i < filter_pixel_count; i++) {
                if (i / 8 < mask_size && ((mask_ptr[i / 8] >> (i % 8)) & 1)) {
                    hi_bytes[i] = (nz_idx < nz_vals.size()) ? nz_vals[nz_idx++] : 0;
                }
            }
        } else {
            hi_bytes = decode_byte_stream(ptr, hi_stream_size, filter_pixel_count);
        }
    }
    const auto t_hi1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_filter_hi_ns : nullptr, t_hi0, t_hi1);
    ptr += hi_stream_size;

    ptr += block_types_size;

    const auto t_pal0 = Clock::now();
    std::vector<Palette> palettes;
    std::vector<std::vector<uint8_t>> palette_indices;
    if (palette_data_size > 0) {
        int num_palette = 0;
        for (auto t : block_types) {
            if (t == FileHeader::BlockType::PALETTE) num_palette++;
        }
        const uint8_t* pal_ptr = ptr;
        size_t pal_size = palette_data_size;
        std::vector<uint8_t> pal_decoded;

        if (file_version >= FileHeader::VERSION_BLOCK_TYPES_V2 &&
            palette_data_size >= 6 && ptr[0] == FileHeader::WRAPPER_MAGIC_PALETTE) {
            uint8_t mode = ptr[1];
            uint32_t raw_count = 0;
            std::memcpy(&raw_count, ptr + 2, 4);
            const uint8_t* enc_ptr = ptr + 6;
            size_t enc_size = palette_data_size - 6;

            if (mode == 1) {
                pal_decoded = decode_byte_stream(enc_ptr, enc_size, raw_count);
            } else if (mode == 2) {
                pal_decoded = TileLZ::decompress(enc_ptr, enc_size, raw_count);
            }

            if (!pal_decoded.empty()) {
                pal_ptr = pal_decoded.data();
                pal_size = pal_decoded.size();
            }
        }
        PaletteCodec::decode_palette_stream(
            pal_ptr, pal_size, palettes, palette_indices, num_palette);
        ptr += palette_data_size;
    }
    const auto t_pal1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_palette_ns : nullptr, t_pal0, t_pal1);

    const auto t_copy0 = Clock::now();
    std::vector<CopyParams> copy_params;
    if (copy_data_size > 0) {
        const uint8_t* cptr = ptr;
        size_t csz = copy_data_size;
        std::vector<uint8_t> unpacked;

        if (csz > 6 && cptr[0] == FileHeader::WRAPPER_MAGIC_COPY) {
            uint8_t mode = cptr[1];
            uint32_t raw_count;
            std::memcpy(&raw_count, cptr + 2, 4);

            if (mode == 1) {
                unpacked = decode_byte_stream(cptr + 6, csz - 6, raw_count);
                if (!unpacked.empty()) {
                    cptr = unpacked.data();
                    csz = unpacked.size();
                }
            } else if (mode == 2) {
                if (TileLZ::decompress_to(cptr + 6, csz - 6, unpacked, raw_count)) {
                    cptr = unpacked.data();
                    csz = unpacked.size();
                }
            }
        }

        int num_copy = 0;
        for (auto t : block_types) {
            if (t == FileHeader::BlockType::COPY) num_copy++;
        }
        CopyCodec::decode_copy_stream(cptr, csz, copy_params, num_copy);
        ptr += copy_data_size;
    }
    const auto t_copy1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_copy_ns : nullptr, t_copy0, t_copy1);

    const auto t_t40 = Clock::now();
    std::vector<lossless_tile4_codec::Tile4Result> tile4_params;
    const uint8_t* end = td + ts;
    const uint8_t* t4_ptr = ptr;
    size_t t4_size = tile4_data_size;
    std::vector<uint8_t> tile4_decoded;
    bool tile4_from_wrapper = false;
    if (file_version >= FileHeader::VERSION_TILE4_WRAPPER &&
        t4_size >= 6 &&
        t4_ptr[0] == FileHeader::WRAPPER_MAGIC_TILE4) {
        uint8_t mode = t4_ptr[1];
        uint32_t raw_count = 0;
        std::memcpy(&raw_count, t4_ptr + 2, 4);
        const uint8_t* payload = t4_ptr + 6;
        size_t payload_size = t4_size - 6;
        if (mode == 1) {
            tile4_decoded = decode_byte_stream(payload, payload_size, raw_count);
        } else if (mode == 2) {
            tile4_decoded = TileLZ::decompress(payload, payload_size, raw_count);
        }
        if (!tile4_decoded.empty()) {
            t4_ptr = tile4_decoded.data();
            t4_size = tile4_decoded.size();
            tile4_from_wrapper = true;
        } else {
            t4_size = 0;
        }
    }
    if (tile4_data_size > 0) {
        bool bad_size = ((t4_size & 1u) != 0);
        if (!tile4_from_wrapper) {
            bad_size = bad_size || (ptr > end) || (t4_size > (size_t)(end - ptr));
        }
        if (bad_size) t4_size = 0;
    }
    if (t4_size > 0) {
        for (size_t i = 0; i < t4_size; i += 2) {
            lossless_tile4_codec::Tile4Result res;
            res.indices[0] = t4_ptr[i] & 0x0F;
            res.indices[1] = (t4_ptr[i] >> 4) & 0x0F;
            res.indices[2] = t4_ptr[i + 1] & 0x0F;
            res.indices[3] = (t4_ptr[i + 1] >> 4) & 0x0F;
            tile4_params.push_back(res);
        }
    }
    const auto t_t41 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_tile4_ns : nullptr, t_t40, t_t41);
    ptr += tile4_data_size;

    const auto t_merge0 = Clock::now();
    std::vector<int16_t> filter_residuals(filter_pixel_count);
    for (size_t i = 0; i < filter_pixel_count; i++) {
        uint16_t zz = (uint16_t)lo_bytes[i] | ((uint16_t)hi_bytes[i] << 8);
        filter_residuals[i] = zigzag_decode_val(zz);
    }
    const auto t_merge1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_residual_merge_ns : nullptr, t_merge0, t_merge1);

    const auto t_recon0 = Clock::now();
    std::vector<int16_t> padded(pad_w * pad_h, 0);

    std::vector<int> block_palette_idx(nb, -1);
    std::vector<int> block_copy_idx(nb, -1);
    std::vector<int> block_tile4_idx(nb, -1);
    int pi = 0, ci = 0, t4i = 0;
    for (int i = 0; i < nb; i++) {
        if (block_types[i] == FileHeader::BlockType::PALETTE) {
            block_palette_idx[i] = pi++;
            if (perf_stats) perf_stats->plane_recon_block_palette_count++;
        } else if (block_types[i] == FileHeader::BlockType::COPY) {
            block_copy_idx[i] = ci++;
            if (perf_stats) perf_stats->plane_recon_block_copy_count++;
        } else if (block_types[i] == FileHeader::BlockType::TILE_MATCH4) {
            block_tile4_idx[i] = t4i++;
            if (perf_stats) perf_stats->plane_recon_block_tile4_count++;
        } else {
            if (perf_stats) perf_stats->plane_recon_block_dct_count++;
        }
    }

    for (int i = 0; i < nb; i++) {
        if (block_types[i] != FileHeader::BlockType::PALETTE) continue;
        int pidx = block_palette_idx[i];
        if (pidx < 0 || pidx >= (int)palettes.size()) continue;
        int bx = i % nx, by = i / nx;
        const auto& p = palettes[pidx];
        const auto& idx = palette_indices[pidx];
        for (int py = 0; py < 8; py++) {
            for (int px = 0; px < 8; px++) {
                int k = py * 8 + px;
                int16_t pal_v = 0;
                if (k < (int)idx.size()) {
                    uint8_t pi2 = idx[(size_t)k];
                    if (pi2 < p.size) pal_v = p.colors[pi2];
                }
                padded[(size_t)(by * 8 + py) * (size_t)pad_w + (size_t)(bx * 8 + px)] = pal_v;
            }
        }
    }

    const CopyParams kTileMatch4Candidates[16] = {
        CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
        CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
        CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
        CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
    };
    const int pad_w_i = (int)pad_w;
    const int pad_h_i = (int)pad_h;

    size_t residual_idx = 0;
    const size_t residual_size = filter_residuals.size();

    for (int by = 0; by < ny; by++) {
        const int block_row_base = by * nx;
        const uint32_t y_base = (uint32_t)(by * 8);
        for (int yoff = 0; yoff < 8; yoff++) {
            const uint32_t y = y_base + (uint32_t)yoff;
            const uint8_t ftype = (y < filter_ids.size()) ? filter_ids[y] : 0;
            const size_t row_base = (size_t)y * (size_t)pad_w;
            const size_t up_row_base = (y > 0) ? ((size_t)(y - 1) * (size_t)pad_w) : 0;

            for (int bx = 0; bx < nx; bx++) {
                const int block_idx = block_row_base + bx;
                const uint32_t x_base = (uint32_t)(bx * 8);
                const auto bt = block_types[(size_t)block_idx];

                if (bt == FileHeader::BlockType::PALETTE) {
                    continue;
                }
                if (bt == FileHeader::BlockType::COPY) {
                    const int cidx = block_copy_idx[(size_t)block_idx];
                    if (cidx < 0 || cidx >= (int)copy_params.size()) continue;
                    const auto& cp = copy_params[(size_t)cidx];
                    const int src_y = (int)y + cp.dy;
                    const int src_x0 = (int)x_base + cp.dx;
                    if (src_y >= 0 && src_y < pad_h_i && src_x0 >= 0 && src_x0 + 7 < pad_w_i) {
                        if (perf_stats) perf_stats->plane_recon_copy_fast_rows++;
                        int16_t* dst = &padded[row_base + (size_t)x_base];
                        int16_t* src = &padded[(size_t)src_y * (size_t)pad_w + (size_t)src_x0];
                        std::memcpy(dst, src, 8 * sizeof(int16_t));
                    } else {
                        if (perf_stats) {
                            perf_stats->plane_recon_copy_slow_rows++;
                            perf_stats->plane_recon_copy_clamped_pixels += 8;
                        }
                        for (uint32_t px = 0; px < 8; px++) {
                            const uint32_t x = x_base + px;
                            int src_x = (int)x + cp.dx;
                            int src_y2 = (int)y + cp.dy;
                            src_x = std::clamp(src_x, 0, pad_w_i - 1);
                            src_y2 = std::clamp(src_y2, 0, pad_h_i - 1);
                            padded[row_base + (size_t)x] =
                                padded[(size_t)src_y2 * (size_t)pad_w + (size_t)src_x];
                        }
                    }
                    continue;
                }
                if (bt == FileHeader::BlockType::TILE_MATCH4) {
                    const int t4idx = block_tile4_idx[(size_t)block_idx];
                    if (t4idx < 0 || t4idx >= (int)tile4_params.size()) continue;
                    const auto& t4 = tile4_params[(size_t)t4idx];
                    const int qy = (yoff >= 4) ? 1 : 0;
                    for (int qx = 0; qx < 2; qx++) {
                        const int q = qy * 2 + qx;
                        const int cand_idx = t4.indices[q];
                        const CopyParams& cand = kTileMatch4Candidates[cand_idx];
                        const uint32_t seg_x_base = x_base + (uint32_t)(qx * 4);
                        const int src_y = (int)y + cand.dy;
                        const int src_x0 = (int)seg_x_base + cand.dx;

                        if (src_y >= 0 && src_y < pad_h_i && src_x0 >= 0 && src_x0 + 3 < pad_w_i) {
                            if (perf_stats) perf_stats->plane_recon_tile4_fast_quads++;
                            int16_t* dst = &padded[row_base + (size_t)seg_x_base];
                            int16_t* src = &padded[(size_t)src_y * (size_t)pad_w + (size_t)src_x0];
                            std::memcpy(dst, src, 4 * sizeof(int16_t));
                        } else {
                            if (perf_stats) {
                                perf_stats->plane_recon_tile4_slow_quads++;
                                perf_stats->plane_recon_tile4_clamped_pixels += 4;
                            }
                            for (uint32_t px = 0; px < 4; px++) {
                                const uint32_t x = seg_x_base + px;
                                int src_x = (int)x + cand.dx;
                                int src_y2 = (int)y + cand.dy;
                                src_x = std::clamp(src_x, 0, pad_w_i - 1);
                                src_y2 = std::clamp(src_y2, 0, pad_h_i - 1);
                                padded[row_base + (size_t)x] =
                                    padded[(size_t)src_y2 * (size_t)pad_w + (size_t)src_x];
                            }
                        }
                    }
                    continue;
                }

                if (perf_stats) perf_stats->plane_recon_dct_pixels += 8;
                int16_t* const dst = padded.data() + row_base + (size_t)x_base;
                const int16_t* const up = (y > 0) ? (padded.data() + up_row_base + (size_t)x_base) : nullptr;

                constexpr size_t kRun = 8;
                if (residual_idx + kRun <= residual_size) {
                    const int16_t* const rs = filter_residuals.data() + residual_idx;
                    switch (ftype) {
                        case 0: {
                            std::memcpy(dst, rs, kRun * sizeof(int16_t));
                            break;
                        }
                        case 1: {
                            int16_t left = (x_base > 0) ? dst[-1] : 0;
                            for (size_t px = 0; px < kRun; px++) {
                                left = (int16_t)(left + rs[px]);
                                dst[px] = left;
                            }
                            break;
                        }
                        case 2: {
                            if (up) {
                                for (size_t px = 0; px < kRun; px++) {
                                    dst[px] = (int16_t)(up[px] + rs[px]);
                                }
                            } else {
                                std::memcpy(dst, rs, kRun * sizeof(int16_t));
                            }
                            break;
                        }
                        case 3: {
                            int16_t left = (x_base > 0) ? dst[-1] : 0;
                            for (size_t px = 0; px < kRun; px++) {
                                const int16_t b = up ? up[px] : 0;
                                const int16_t pred = (int16_t)(((int)left + (int)b) / 2);
                                const int16_t cur = (int16_t)(pred + rs[px]);
                                dst[px] = cur;
                                left = cur;
                            }
                            break;
                        }
                        case 4: {
                            int16_t left = (x_base > 0) ? dst[-1] : 0;
                            int16_t up_left = (up && x_base > 0) ? up[-1] : 0;
                            for (size_t px = 0; px < kRun; px++) {
                                const int16_t b = up ? up[px] : 0;
                                const int16_t pred = LosslessFilter::paeth_predictor(left, b, up_left);
                                const int16_t cur = (int16_t)(pred + rs[px]);
                                dst[px] = cur;
                                left = cur;
                                up_left = b;
                            }
                            break;
                        }
                        case 5: {
                            int16_t left = (x_base > 0) ? dst[-1] : 0;
                            int16_t up_left = (up && x_base > 0) ? up[-1] : 0;
                            for (size_t px = 0; px < kRun; px++) {
                                const int16_t b = up ? up[px] : 0;
                                const int16_t pred = LosslessFilter::med_predictor(left, b, up_left);
                                const int16_t cur = (int16_t)(pred + rs[px]);
                                dst[px] = cur;
                                left = cur;
                                up_left = b;
                            }
                            break;
                        }
                        case 6: {
                            int16_t left = (x_base > 0) ? dst[-1] : 0;
                            for (size_t px = 0; px < kRun; px++) {
                                const int16_t b = up ? up[px] : 0;
                                const int16_t pred = (int16_t)(((int)left * 3 + (int)b) / 4);
                                const int16_t cur = (int16_t)(pred + rs[px]);
                                dst[px] = cur;
                                left = cur;
                            }
                            break;
                        }
                        case 7: {
                            int16_t left = (x_base > 0) ? dst[-1] : 0;
                            for (size_t px = 0; px < kRun; px++) {
                                const int16_t b = up ? up[px] : 0;
                                const int16_t pred = (int16_t)(((int)left + (int)b * 3) / 4);
                                const int16_t cur = (int16_t)(pred + rs[px]);
                                dst[px] = cur;
                                left = cur;
                            }
                            break;
                        }
                        default: {
                            std::memcpy(dst, rs, kRun * sizeof(int16_t));
                            break;
                        }
                    }
                    residual_idx += kRun;
                    if (perf_stats) perf_stats->plane_recon_residual_consumed += kRun;
                    continue;
                }

                for (uint32_t px = 0; px < 8; px++) {
                    const uint32_t x = x_base + px;
                    const size_t pos = row_base + (size_t)x;
                    const int16_t a = (x > 0) ? padded[pos - 1] : 0;
                    const int16_t b = (y > 0) ? padded[up_row_base + (size_t)x] : 0;
                    const int16_t c = (x > 0 && y > 0) ? padded[up_row_base + (size_t)(x - 1)] : 0;
                    int16_t pred = 0;
                    switch (ftype) {
                        case 0: pred = 0; break;
                        case 1: pred = a; break;
                        case 2: pred = b; break;
                        case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                        case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                        case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                        case 6: pred = (int16_t)(((int)a * 3 + (int)b) / 4); break;
                        case 7: pred = (int16_t)(((int)a + (int)b * 3) / 4); break;
                        default: pred = 0; break;
                    }
                    if (residual_idx < residual_size) {
                        padded[pos] = filter_residuals[residual_idx++] + pred;
                        if (perf_stats) perf_stats->plane_recon_residual_consumed++;
                    } else if (perf_stats) {
                        perf_stats->plane_recon_residual_missing++;
                    }
                }
            }
        }
    }
    const auto t_recon1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_reconstruct_ns : nullptr, t_recon0, t_recon1);

    const auto t_crop0 = Clock::now();
    std::vector<int16_t> result(width * height);
    for (uint32_t y = 0; y < height; y++) {
        std::memcpy(&result[(size_t)y * (size_t)width],
                    &padded[(size_t)y * (size_t)pad_w],
                    width * sizeof(int16_t));
    }
    const auto t_crop1 = Clock::now();
    add_ns(perf_stats ? &perf_stats->plane_crop_ns : nullptr, t_crop0, t_crop1);

    return result;
}

} // namespace hakonyans::lossless_plane_decode_core
