#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/parallel_decode.h"
#include "../simd/simd_dispatch.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <future>
#include <thread>
#include <cmath>
#include "palette.h"
#include "copy.h"
#include "lossless_filter.h"

namespace hakonyans {

class GrayscaleDecoder {
public:
    static std::vector<uint8_t> pad_image(const uint8_t* p, uint32_t w, uint32_t h, uint32_t pw, uint32_t ph) {
        std::vector<uint8_t> out(pw * ph); for (uint32_t y = 0; y < ph; y++) for (uint32_t x = 0; x < pw; x++) out[y * pw + x] = p[std::min(y, h-1) * w + std::min(x, w-1)]; return out;
    }

    static void upsample_420_bilinear(const uint8_t* s, int w, int h, std::vector<uint8_t>& d, int dw, int dh) {
        d.resize(dw * dh);
        for (int y = 0; y < dh; y++) {
            for (int x = 0; x < dw; x++) {
                float sx = (float)x * (w - 1) / (dw - 1), sy = (float)y * (h - 1) / (dh - 1);
                int x0 = (int)sx, y0 = (int)sy, x1 = std::min(x0+1, w-1), y1 = std::min(y0+1, h-1);
                float fx = sx - x0, fy = sy - y0;
                float v = (s[y0*w+x0]*(1-fx) + s[y0*w+x1]*fx)*(1-fy) + (s[y1*w+x0]*(1-fx) + s[y1*w+x1]*fx)*fy;
                d[y*dw+x] = (uint8_t)(v + 0.5f);
            }
        }
    }

    static std::vector<uint8_t> decode(const std::vector<uint8_t>& hkn) {
        FileHeader hdr = FileHeader::read(hkn.data());
        // Dispatch to lossless if flags bit0 is set
        if (hdr.flags & 1) return decode_lossless(hkn);
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* qm_e = dir.find("QMAT"); QMATChunk qm = QMATChunk::deserialize(&hkn[qm_e->offset], qm_e->size);
        uint16_t deq[64]; std::memcpy(deq, qm.quant_y, 128);
        const ChunkEntry* t_e = dir.find("TIL0"); if (!t_e) t_e = dir.find("TILE");
        auto pad = decode_plane(&hkn[t_e->offset], t_e->size, hdr.padded_width(), hdr.padded_height(), deq);
        std::vector<uint8_t> out(hdr.width * hdr.height);
        for (uint32_t y = 0; y < hdr.height; y++) std::memcpy(&out[y * hdr.width], &pad[y * hdr.padded_width()], hdr.width);
        return out;
    }

    static std::vector<uint8_t> decode_color(const std::vector<uint8_t>& hkn, int& w, int& h) {
        FileHeader hdr = FileHeader::read(hkn.data()); w = hdr.width; h = hdr.height;
        // Dispatch to lossless if flags bit0 is set
        if (hdr.flags & 1) return decode_color_lossless(hkn, w, h);
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* qm_e = dir.find("QMAT"); QMATChunk qm = QMATChunk::deserialize(&hkn[qm_e->offset], qm_e->size);
        uint16_t deq[64]; std::memcpy(deq, qm.quant_y, 128);
        const ChunkEntry* t0 = dir.find("TIL0"), *t1 = dir.find("TIL1"), *t2 = dir.find("TIL2");
        bool is_420 = (hdr.subsampling == 1), is_cfl = (hdr.flags & 2);
        int cw = is_420 ? (w + 1) / 2 : w, ch = is_420 ? (h + 1) / 2 : h;
        uint32_t pyw = hdr.padded_width(), pyh = hdr.padded_height();
        uint32_t pcw = ((cw + 7) / 8) * 8, pch = ((ch + 7) / 8) * 8;
        auto yp_v = decode_plane(&hkn[t0->offset], t0->size, pyw, pyh, deq);
        std::vector<uint8_t> y_ref;
        if (is_cfl) {
            if (is_420) {
                std::vector<uint8_t> y_full(w * h), y_ds; int ydw, ydh;
                for (int y = 0; y < h; y++) std::memcpy(&y_full[y * w], &yp_v[y * pyw], w);
                downsample_420(y_full.data(), w, h, y_ds, ydw, ydh);
                y_ref = pad_image(y_ds.data(), ydw, ydh, pcw, pch);
            } else y_ref = yp_v;
        }
        auto f1 = std::async(std::launch::async, [=, &hkn, &y_ref]() { return decode_plane(&hkn[t1->offset], t1->size, pcw, pch, deq, is_cfl ? &y_ref : nullptr); });
        auto f2 = std::async(std::launch::async, [=, &hkn, &y_ref]() { return decode_plane(&hkn[t2->offset], t2->size, pcw, pch, deq, is_cfl ? &y_ref : nullptr); });
        auto cb_raw = f1.get(); auto cr_raw = f2.get();
        std::vector<uint8_t> y_p(w * h), cb_p(w * h), cr_p(w * h);
        for (int y = 0; y < h; y++) std::memcpy(&y_p[y * w], &yp_v[y * pyw], w);
        if (is_420) {
            std::vector<uint8_t> cbc(cw * ch), crc(cw * ch);
            for (int y = 0; y < ch; y++) { std::memcpy(&cbc[y * cw], &cb_raw[y * pcw], cw); std::memcpy(&crc[y * cw], &cr_raw[y * pcw], cw); }
            upsample_420_bilinear(cbc.data(), cw, ch, cb_p, w, h); upsample_420_bilinear(crc.data(), cw, ch, cr_p, w, h);
        } else {
            for (int y = 0; y < h; y++) { std::memcpy(&cb_p[y * w], &cb_raw[y * pyw], w); std::memcpy(&cr_p[y * w], &cr_raw[y * pyw], w); }
        }
        std::vector<uint8_t> rgb(w * h * 3); unsigned int nt = std::thread::hardware_concurrency(); if (nt == 0) nt = 4; nt = std::min<unsigned int>(nt, 8); nt = std::max(1u, std::min<unsigned int>(nt, (unsigned int)h));
        std::vector<std::future<void>> futs; int rpt = h / nt;
        for (unsigned int t = 0; t < nt; t++) {
            int sy = t * rpt, ey = (t == nt - 1) ? h : (t + 1) * rpt;
            futs.push_back(std::async(std::launch::async, [=, &y_p, &cb_p, &cr_p, &rgb]() {
                for (int y = sy; y < ey; y++) {
                    simd::ycbcr_to_rgb_row(&y_p[y*w], &cb_p[y*w], &cr_p[y*w], &rgb[y*w*3], w);
                }
            }));
        }
        for (auto& f : futs) f.get(); return rgb;
    }

public:
    static std::vector<uint8_t> decode_plane(const uint8_t* td, size_t ts, uint32_t pw, uint32_t ph, const uint16_t deq[64], const std::vector<uint8_t>* y_ref = nullptr) {
        uint32_t sz[8]; std::memcpy(sz, td, 32); const uint8_t* ptr = td + 32;
        auto dcs = decode_stream(ptr, sz[0]); ptr += sz[0];
        // sz[1]=ac, sz[2]=pi, sz[3]=q, sz[4]=cfl, sz[5]=bt, sz[6]=pal, sz[7]=cpy
        std::vector<Token> acs;
        if (sz[2] > 0) {
            PIndex pi = PIndexCodec::deserialize(std::span<const uint8_t>(td + 32 + sz[0] + sz[1], sz[2]));
            acs = decode_stream_parallel(ptr, sz[1], pi);
        } else {
            acs = decode_stream(ptr, sz[1]);
        }
        ptr += sz[1] + sz[2];
        std::vector<int8_t> qds; if (sz[3] > 0) { qds.resize(sz[3]); std::memcpy(qds.data(), ptr, sz[3]); ptr += sz[3]; }
        std::vector<CfLParams> cfls; if (sz[4] > 0) { for (uint32_t i=0; i<sz[4]/2; i++) { float a = (int8_t)ptr[i*2]/64.0f, b = ptr[i*2+1]; cfls.push_back({a, b, a, b}); } }
        ptr += sz[4];

        int nx = pw/8, nb = nx*(ph/8); std::vector<uint8_t> pad(pw*ph);

        std::vector<FileHeader::BlockType> block_types;
        if (sz[5] > 0) {
            block_types = decode_block_types(ptr, sz[5], nb);
            ptr += sz[5];
        } else {
            block_types.assign(nb, FileHeader::BlockType::DCT);
        }
        
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        if (sz[6] > 0) {
            int num_palette_blocks = 0;
            for(auto t : block_types) if (t == FileHeader::BlockType::PALETTE) num_palette_blocks++;
            PaletteCodec::decode_palette_stream(ptr, sz[6], palettes, palette_indices, num_palette_blocks);
            ptr += sz[6];
        }

        std::vector<CopyParams> copy_params;
        if (sz[7] > 0) {
            int num_copy = 0;
            for(auto t : block_types) if (t == FileHeader::BlockType::COPY) num_copy++;
            CopyCodec::decode_copy_stream(ptr, sz[7], copy_params, num_copy);
            ptr += sz[7];
        }
        
        std::vector<uint32_t> block_starts(nb + 1);
        size_t cur = 0;
        for (int i = 0; i < nb; i++) {
            block_starts[i] = (uint32_t)cur;
            if (block_types[i] == FileHeader::BlockType::DCT) {
                while (cur < acs.size()) {
                    if (acs[cur++].type == TokenType::ZRUN_63) break;
                    if (cur < acs.size() && (int)acs[cur-1].type < 64) cur++; // Skip MAGC
                }
            }
        }
        block_starts[nb] = (uint32_t)cur;

        // Threading logic:
        // If Copy Mode is used (sz[7] > 0), we force sequential decoding (nt=1) 
        // to ensure Intra-Block Copy vectors point to already-decoded pixels.
        unsigned int nt = std::thread::hardware_concurrency(); if (nt == 0) nt = 4; nt = std::min<unsigned int>(nt, 8); nt = std::max(1u, std::min<unsigned int>(nt, (unsigned int)nb));
        if (sz[7] > 0) {
            nt = 1;
        }

        std::vector<std::future<void>> futs; int bpt = nb / nt;
        for (unsigned int t = 0; t < nt; t++) {
            int sb = t * bpt, eb = (t == nt - 1) ? nb : (t + 1) * bpt;
            futs.push_back(std::async(std::launch::async, [=, &dcs, &acs, &block_starts, &qds, &cfls, &pad, &block_types, &palettes, &palette_indices, &copy_params, deq, y_ref]() {
                // Initialize pdc with correct value for the start of this thread's block range
                int16_t pdc = 0; 
                int palette_block_idx = 0;
                int copy_block_idx = 0;
                
                int dct_block_idx = 0;
                
                // Pre-scan to find correct indices for palette/copy logic
                for (int i = 0; i < sb; i++) {
                     if (block_types[i] == FileHeader::BlockType::DCT) {
                         pdc += Tokenizer::detokenize_dc(dcs[dct_block_idx]);
                         dct_block_idx++;
                     } else if (block_types[i] == FileHeader::BlockType::PALETTE) {
                         palette_block_idx++;
                     } else if (block_types[i] == FileHeader::BlockType::COPY) {
                         copy_block_idx++;
                     }
                }
                
                int16_t ac[63];
                for (int i = sb; i < eb; i++) {
                    int bx = i % nx, by = i / nx;
                    
                    if (block_types[i] == FileHeader::BlockType::DCT) {
                        int16_t dc = pdc + Tokenizer::detokenize_dc(dcs[dct_block_idx]); pdc = dc;
                        dct_block_idx++;
                        uint32_t start = block_starts[i], end = block_starts[i+1];
                        std::fill(ac, ac + 63, 0); int pos = 0;
                        for (uint32_t k = start; k < end && pos < 63; ++k) {
                            const Token& tok = acs[k];
                            if (tok.type == TokenType::ZRUN_63) break;
                            if ((int)tok.type <= 62) {
                                pos += (int)tok.type;
                                if (++k >= end) break;
                                const Token& mt = acs[k];
                                int magc = (int)mt.type - 64;
                                uint16_t sign = (mt.raw_bits >> magc) & 1;
                                uint16_t rem = mt.raw_bits & ((1 << magc) - 1);
                                uint16_t abs_v = (magc > 0) ? ((1 << (magc - 1)) + rem) : 0;
                                if (pos < 63) ac[pos++] = (sign == 0) ? abs_v : -abs_v;
                            }
                        }
                        float s = 1.0f; if (!qds.empty()) s = 1.0f + qds[i] / 50.0f;
                        int16_t dq[64]; dq[0] = dc * (uint16_t)std::max(1.0f, std::round(deq[0]*s));
                        for (int k = 1; k < 64; k++) dq[k] = ac[k-1] * (uint16_t)std::max(1.0f, std::round(deq[k]*s));
                        int16_t co[64], bl[64]; Zigzag::inverse_scan(dq, co); DCT::inverse(co, bl);
                        if (y_ref && !cfls.empty()) {
                            float a = cfls[i].alpha_cb, b = cfls[i].beta_cb;
                            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { int py = (*y_ref)[(by*8+y)*pw+(bx*8+x)]; pad[(by*8+y)*pw+(bx*8+x)] = std::clamp((int)bl[y*8+x] + (int)std::round(a*py+b), 0, 255); }
                        } else { for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) pad[(by*8+y)*pw+(bx*8+x)] = (uint8_t)std::clamp(bl[y*8+x]+128, 0, 255); }
                    } else if (block_types[i] == FileHeader::BlockType::PALETTE) {
                        if (palette_block_idx < (int)palettes.size()) {
                            const auto& p = palettes[palette_block_idx];
                            const auto& idx = palette_indices[palette_block_idx];
                            for (int y = 0; y < 8; y++) {
                                for (int x = 0; x < 8; x++) {
                                    int k = y * 8 + x;
                                    uint8_t color = (k < (int)idx.size()) ? p.colors[idx[k]] : 0;
                                    pad[(by * 8 + y) * pw + (bx * 8 + x)] = color;
                                }
                            }
                            palette_block_idx++;
                        }
                    } else if (block_types[i] == FileHeader::BlockType::COPY) {
                        if (copy_block_idx < (int)copy_params.size()) {
                            CopyParams cp = copy_params[copy_block_idx];
                            for (int y = 0; y < 8; y++) {
                                for (int x = 0; x < 8; x++) {
                                    int dst_x = bx * 8 + x;
                                    int dst_y = by * 8 + y;
                                    int src_x = dst_x + cp.dx;
                                    int src_y = dst_y + cp.dy;
                                    
                                    // Boundary checks
                                    // Should we clamp or replicate or black?
                                    // Standard clamp
                                    src_x = std::clamp(src_x, 0, (int)pw - 1);
                                    src_y = std::clamp(src_y, 0, (int)ph - 1);
                                    
                                    pad[dst_y * pw + dst_x] = pad[src_y * pw + src_x];
                                }
                            }
                            copy_block_idx++;
                        }
                    } else {
                         // Unknown block type?
                    }
                }
            }));
        }
        for (auto& f : futs) f.get(); return pad;
    }

public:
    static std::vector<FileHeader::BlockType> decode_block_types(const uint8_t* val, size_t sz, int nb) {
        std::vector<FileHeader::BlockType> out;
        out.reserve(nb);
        for (size_t i = 0; i < sz; i++) {
            uint8_t v = val[i];
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

    static std::vector<Token> decode_stream(const uint8_t* s, size_t sz) {
        if (sz < 8) return {};
        uint32_t cs; std::memcpy(&cs, s, 4);
        std::vector<uint32_t> f(cs/4); std::memcpy(f.data(), s+4, cs);
        CDFTable cdf = CDFBuilder().build_from_freq(f);
        uint32_t tc; std::memcpy(&tc, s+4+cs, 4);
        uint32_t rs; std::memcpy(&rs, s+8+cs, 4);
        FlatInterleavedDecoder dec(std::span<const uint8_t>(s+12+cs, rs));
        std::vector<Token> t; t.reserve(tc);
        for (uint32_t i=0; i<tc; i++) t.emplace_back((TokenType)dec.decode_symbol(cdf), 0, 0);
        uint32_t rc; size_t off = 12+cs+rs; std::memcpy(&rc, s+off, 4); off += 4;
        size_t ri = 0; for (auto& x : t) if ((int)x.type >= 64 && (int)x.type > 64) { if (ri < rc) { x.raw_bits_count = s[off]; x.raw_bits = s[off+1] | (s[off+2]<<8); off += 3; ri++; } }
        return t;
    }

    static std::vector<Token> decode_stream_parallel(const uint8_t* s, size_t sz, const PIndex& pi) {
        if (sz < 8) return {};
        uint32_t cs; std::memcpy(&cs, s, 4);
        std::vector<uint32_t> f(cs/4); std::memcpy(f.data(), s+4, cs);
        CDFTable cdf = CDFBuilder().build_from_freq(f);
        uint32_t tc; std::memcpy(&tc, s+4+cs, 4);
        uint32_t rs; std::memcpy(&rs, s+8+cs, 4);
        unsigned int nt = std::thread::hardware_concurrency();
        if (nt == 0) nt = 4;
        nt = std::min<unsigned int>(nt, 8);
        auto syms = ParallelDecoder::decode(std::span<const uint8_t>(s+12+cs, rs), pi, cdf, nt);
        std::vector<Token> t; t.reserve(tc);
        for (int x : syms) t.emplace_back((TokenType)x, 0, 0);
        uint32_t rc; size_t off = 12+cs+rs; std::memcpy(&rc, s+off, 4); off += 4;
        size_t ri = 0; for (auto& x : t) if ((int)x.type >= 64 && (int)x.type > 64) { if (ri < rc) { x.raw_bits_count = s[off]; x.raw_bits = s[off+1] | (s[off+2]<<8); off += 3; ri++; } }
        return t;
    }

    // ========================================================================
    // Lossless decoding
    // ========================================================================

    /**
     * Decode a lossless grayscale .hkn file.
     */
    static std::vector<uint8_t> decode_lossless(const std::vector<uint8_t>& hkn) {
        FileHeader hdr = FileHeader::read(hkn.data());
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* t0 = dir.find("TIL0");
        auto plane = decode_plane_lossless(&hkn[t0->offset], t0->size, hdr.width, hdr.height);

        // int16_t -> uint8_t
        std::vector<uint8_t> out(hdr.width * hdr.height);
        for (size_t i = 0; i < out.size(); i++) {
            out[i] = (uint8_t)std::clamp((int)plane[i], 0, 255);
        }
        return out;
    }

    /**
     * Decode a lossless color .hkn file (YCoCg-R).
     */
    static std::vector<uint8_t> decode_color_lossless(const std::vector<uint8_t>& hkn, int& w, int& h) {
        FileHeader hdr = FileHeader::read(hkn.data());
        w = hdr.width; h = hdr.height;
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* t0 = dir.find("TIL0");
        const ChunkEntry* t1 = dir.find("TIL1");
        const ChunkEntry* t2 = dir.find("TIL2");

        auto y_plane  = decode_plane_lossless(&hkn[t0->offset], t0->size, w, h);
        auto co_plane = decode_plane_lossless(&hkn[t1->offset], t1->size, w, h);
        auto cg_plane = decode_plane_lossless(&hkn[t2->offset], t2->size, w, h);

        // YCoCg-R -> RGB
        std::vector<uint8_t> rgb(w * h * 3);
        for (int i = 0; i < w * h; i++) {
            ycocg_r_to_rgb(y_plane[i], co_plane[i], cg_plane[i],
                           rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
        }
        return rgb;
    }

    /**
     * Decode a single lossless plane with Screen Profile support.
     *
     * Tile format v2 (32-byte header):
     *   [4B filter_ids_size][4B lo_stream_size][4B hi_stream_size][4B filter_pixel_count]
     *   [4B block_types_size][4B palette_data_size][4B copy_data_size][4B reserved]
     *   [filter_ids][lo_stream][hi_stream][block_types][palette_data][copy_data]
     */
    static std::vector<int16_t> decode_plane_lossless(
        const uint8_t* td, size_t ts, uint32_t width, uint32_t height
    ) {
        // Pad dimensions to multiple of 8
        uint32_t pad_w = ((width + 7) / 8) * 8;
        uint32_t pad_h = ((height + 7) / 8) * 8;
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;

        // Read tile header (8 x uint32_t = 32 bytes)
        uint32_t hdr[8];
        std::memcpy(hdr, td, 32);
        uint32_t filter_ids_size = hdr[0];
        uint32_t lo_stream_size  = hdr[1];
        uint32_t hi_stream_size  = hdr[2];
        uint32_t filter_pixel_count = hdr[3];
        uint32_t block_types_size = hdr[4];
        uint32_t palette_data_size = hdr[5];
        uint32_t copy_data_size = hdr[6];
        // hdr[7] = reserved

        const uint8_t* ptr = td + 32;

        // Read filter IDs
        std::vector<uint8_t> filter_ids(ptr, ptr + filter_ids_size);
        ptr += filter_ids_size;

        // Decode rANS byte streams (data-adaptive CDF)
        std::vector<uint8_t> lo_bytes, hi_bytes;
        if (lo_stream_size > 0 && filter_pixel_count > 0) {
            lo_bytes = decode_byte_stream(ptr, lo_stream_size, filter_pixel_count);
        }
        ptr += lo_stream_size;
        if (hi_stream_size > 0 && filter_pixel_count > 0) {
            hi_bytes = decode_byte_stream(ptr, hi_stream_size, filter_pixel_count);
        }
        ptr += hi_stream_size;

        // Decode block types
        std::vector<FileHeader::BlockType> block_types;
        if (block_types_size > 0) {
            block_types = decode_block_types(ptr, block_types_size, nb);
            ptr += block_types_size;
        } else {
            block_types.assign(nb, FileHeader::BlockType::DCT);
        }

        // Decode palette data
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        if (palette_data_size > 0) {
            int num_palette = 0;
            for (auto t : block_types) if (t == FileHeader::BlockType::PALETTE) num_palette++;
            PaletteCodec::decode_palette_stream(ptr, palette_data_size, palettes, palette_indices, num_palette);
            ptr += palette_data_size;
        }

        // Decode copy data
        std::vector<CopyParams> copy_params;
        if (copy_data_size > 0) {
            int num_copy = 0;
            for (auto t : block_types) if (t == FileHeader::BlockType::COPY) num_copy++;
            CopyCodec::decode_copy_stream(ptr, copy_data_size, copy_params, num_copy);
            ptr += copy_data_size;
        }

        // Combine lo/hi -> uint16_t -> zigzag decode -> int16_t (filter residuals)
        std::vector<int16_t> filter_residuals(filter_pixel_count);
        for (size_t i = 0; i < filter_pixel_count; i++) {
            uint16_t zz = (uint16_t)lo_bytes[i] | ((uint16_t)hi_bytes[i] << 8);
            filter_residuals[i] = zigzag_decode_val(zz);
        }

        // --- Custom unfilter with block-type awareness ---
        // 1. Pre-fill Palette blocks (no dependencies)
        // 2. Process in raster order: Palette already filled, Copy from earlier data, Filter via unfilter
        std::vector<int16_t> padded(pad_w * pad_h, 0);

        // Build per-block lookup for palette and copy
        std::vector<int> block_palette_idx(nb, -1);
        std::vector<int> block_copy_idx(nb, -1);
        int pi = 0, ci = 0;
        for (int i = 0; i < nb; i++) {
            if (block_types[i] == FileHeader::BlockType::PALETTE) block_palette_idx[i] = pi++;
            else if (block_types[i] == FileHeader::BlockType::COPY) block_copy_idx[i] = ci++;
        }

        // Pre-fill all Palette blocks
        for (int i = 0; i < nb; i++) {
            if (block_types[i] != FileHeader::BlockType::PALETTE) continue;
            int pidx = block_palette_idx[i];
            if (pidx < 0 || pidx >= (int)palettes.size()) continue;
            int bx = i % nx, by = i / nx;
            const auto& p = palettes[pidx];
            const auto& idx = palette_indices[pidx];
            for (int py = 0; py < 8; py++)
                for (int px = 0; px < 8; px++)
                    padded[(by * 8 + py) * pad_w + (bx * 8 + px)] =
                        (int16_t)p.colors[idx[py * 8 + px]] - 128;
        }

        // Process rows in raster order: Copy and Filter blocks
        size_t residual_idx = 0;
        for (uint32_t y = 0; y < pad_h; y++) {
            uint8_t ftype = (y < filter_ids.size()) ? filter_ids[y] : 0;
            for (uint32_t x = 0; x < pad_w; x++) {
                int bx_col = x / 8, by_row = y / 8;
                int block_idx = by_row * nx + bx_col;

                if (block_types[block_idx] == FileHeader::BlockType::PALETTE) {
                    // Already filled
                    continue;
                } else if (block_types[block_idx] == FileHeader::BlockType::COPY) {
                    int cidx = block_copy_idx[block_idx];
                    if (cidx >= 0 && cidx < (int)copy_params.size()) {
                        int src_x = (int)x + copy_params[cidx].dx;
                        int src_y = (int)y + copy_params[cidx].dy;
                        src_x = std::clamp(src_x, 0, (int)pad_w - 1);
                        src_y = std::clamp(src_y, 0, (int)pad_h - 1);
                        padded[y * pad_w + x] = padded[src_y * pad_w + src_x];
                    }
                } else {
                    // Filter block: unfilter using prediction + residual
                    int16_t a = (x > 0) ? padded[y * pad_w + x - 1] : 0;
                    int16_t b = (y > 0) ? padded[(y - 1) * pad_w + x] : 0;
                    int16_t c = (x > 0 && y > 0) ? padded[(y - 1) * pad_w + x - 1] : 0;
                    int16_t pred;
                    switch (ftype) {
                        case 0: pred = 0; break;
                        case 1: pred = a; break;
                        case 2: pred = b; break;
                        case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                        case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                        default: pred = 0; break;
                    }
                    if (residual_idx < filter_residuals.size()) {
                        padded[y * pad_w + x] = filter_residuals[residual_idx++] + pred;
                    }
                }
            }
        }

        // Crop to original dimensions
        std::vector<int16_t> result(width * height);
        for (uint32_t y = 0; y < height; y++) {
            std::memcpy(&result[y * width], &padded[y * pad_w], width * sizeof(int16_t));
        }

        return result;
    }


    /**
     * Decode a rANS-encoded byte stream with data-adaptive CDF.
     * Format: [4B cdf_size][cdf_data][4B count][4B rans_size][rans_data]
     */
    static std::vector<uint8_t> decode_byte_stream(
        const uint8_t* data, size_t size, size_t expected_count
    ) {
        if (size < 12) return std::vector<uint8_t>(expected_count, 0);

        uint32_t cdf_size;
        std::memcpy(&cdf_size, data, 4);

        std::vector<uint32_t> freq(cdf_size / 4);
        std::memcpy(freq.data(), data + 4, cdf_size);
        CDFTable cdf = CDFBuilder().build_from_freq(freq);

        uint32_t count;
        std::memcpy(&count, data + 4 + cdf_size, 4);

        uint32_t rans_size;
        std::memcpy(&rans_size, data + 8 + cdf_size, 4);

        FlatInterleavedDecoder dec(std::span<const uint8_t>(data + 12 + cdf_size, rans_size));

        std::vector<uint8_t> result;
        result.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            result.push_back((uint8_t)dec.decode_symbol(cdf));
        }
        return result;
    }
};

} // namespace hakonyans
