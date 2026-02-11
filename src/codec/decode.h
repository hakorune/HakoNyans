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
#include "band_groups.h"
#include "lz_tile.h"

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
        auto pad = decode_plane(&hkn[t_e->offset], t_e->size, hdr.padded_width(), hdr.padded_height(), deq, nullptr, hdr.version);
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
        uint16_t deq_y[64], deq_cb[64], deq_cr[64];
        std::memcpy(deq_y, qm.quant_y, 128);
        if (qm.num_tables == 3) {
            std::memcpy(deq_cb, qm.quant_cb, 128);
            std::memcpy(deq_cr, qm.quant_cr, 128);
        } else {
            std::memcpy(deq_cb, qm.quant_y, 128);
            std::memcpy(deq_cr, qm.quant_y, 128);
        }
        const ChunkEntry* t0 = dir.find("TIL0"), *t1 = dir.find("TIL1"), *t2 = dir.find("TIL2");
        bool is_420 = (hdr.subsampling == 1), is_cfl = (hdr.flags & 2);
        int cw = is_420 ? (w + 1) / 2 : w, ch = is_420 ? (h + 1) / 2 : h;
        uint32_t pyw = hdr.padded_width(), pyh = hdr.padded_height();
        uint32_t pcw = ((cw + 7) / 8) * 8, pch = ((ch + 7) / 8) * 8;
        auto yp_v = decode_plane(&hkn[t0->offset], t0->size, pyw, pyh, deq_y, nullptr, hdr.version);
        std::vector<uint8_t> y_ref;
        if (is_cfl) {
            if (is_420) {
                std::vector<uint8_t> y_full(w * h), y_ds; int ydw, ydh;
                for (int y = 0; y < h; y++) std::memcpy(&y_full[y * w], &yp_v[y * pyw], w);
                downsample_420(y_full.data(), w, h, y_ds, ydw, ydh);
                y_ref = pad_image(y_ds.data(), ydw, ydh, pcw, pch);
            } else y_ref = yp_v;
        }
        auto f1 = std::async(std::launch::async, [=, &hkn, &y_ref]() { return decode_plane(&hkn[t1->offset], t1->size, pcw, pch, deq_cb, is_cfl ? &y_ref : nullptr, hdr.version); });
        auto f2 = std::async(std::launch::async, [=, &hkn, &y_ref]() { return decode_plane(&hkn[t2->offset], t2->size, pcw, pch, deq_cr, is_cfl ? &y_ref : nullptr, hdr.version); });
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
    struct BandPIndexBundle {
        bool has_low = false;
        bool has_mid = false;
        bool has_high = false;
        PIndex low;
        PIndex mid;
        PIndex high;
    };

    static bool parse_band_pindex_blob(
        const uint8_t* data, size_t size,
        size_t low_stream_size, size_t mid_stream_size, size_t high_stream_size,
        BandPIndexBundle& out
    ) {
        if (size == 0) return false;
        if (size < 12) return false;

        uint32_t low_sz = 0, mid_sz = 0, high_sz = 0;
        std::memcpy(&low_sz, data, 4);
        std::memcpy(&mid_sz, data + 4, 4);
        std::memcpy(&high_sz, data + 8, 4);
        size_t expected = 12ull + (size_t)low_sz + (size_t)mid_sz + (size_t)high_sz;
        if (expected != size) return false;

        const uint8_t* ptr = data + 12;
        try {
            if (low_sz > 0) {
                out.low = PIndexCodec::deserialize(std::span<const uint8_t>(ptr, low_sz));
                out.has_low = (out.low.total_bytes == low_stream_size && out.low.total_tokens > 0);
            }
            ptr += low_sz;
            if (mid_sz > 0) {
                out.mid = PIndexCodec::deserialize(std::span<const uint8_t>(ptr, mid_sz));
                out.has_mid = (out.mid.total_bytes == mid_stream_size && out.mid.total_tokens > 0);
            }
            ptr += mid_sz;
            if (high_sz > 0) {
                out.high = PIndexCodec::deserialize(std::span<const uint8_t>(ptr, high_sz));
                out.has_high = (out.high.total_bytes == high_stream_size && out.high.total_tokens > 0);
            }
        } catch (...) {
            return false;
        }

        return out.has_low || out.has_mid || out.has_high;
    }

    static void parse_cfl_stream(
        const uint8_t* cfl_ptr,
        uint32_t sz_cfl,
        int nb,
        std::vector<CfLParams>& cfls,
        bool& centered_predictor
    ) {
        centered_predictor = false;
        cfls.clear();
        if (!cfl_ptr || sz_cfl == 0 || nb <= 0) return;

        const size_t nb_sz = (size_t)nb;
        const size_t legacy_size = nb_sz * 2;
        const size_t mask_bytes = (nb_sz + 7) / 8;

        auto parse_legacy = [&]() {
            size_t pairs = std::min(nb_sz, (size_t)sz_cfl / 2);
            cfls.reserve(nb_sz);
            for (size_t i = 0; i < pairs; i++) {
                float a = (int8_t)cfl_ptr[i * 2] / 64.0f;
                float b = cfl_ptr[i * 2 + 1];
                // Legacy stream applies predictor for every block.
                cfls.push_back({a, b, 1.0f, 0.0f});
            }
            if (pairs < nb_sz) {
                cfls.resize(nb_sz, {0.0f, 128.0f, 0.0f, 0.0f});
            }
            centered_predictor = false;
        };

        auto try_parse_adaptive = [&]() -> bool {
            if ((size_t)sz_cfl < mask_bytes) return false;
            size_t applied = 0;
            for (int i = 0; i < nb; i++) {
                if (cfl_ptr[(size_t)i / 8] & (uint8_t)(1u << (i % 8))) applied++;
            }
            size_t expected = mask_bytes + applied * 2;
            if (expected != (size_t)sz_cfl) return false;

            cfls.assign(nb_sz, {0.0f, 128.0f, 0.0f, 0.0f});
            const uint8_t* param_ptr = cfl_ptr + mask_bytes;
            for (int i = 0; i < nb; i++) {
                if (cfl_ptr[(size_t)i / 8] & (uint8_t)(1u << (i % 8))) {
                    float a = (int8_t)(*param_ptr++) / 64.0f;
                    float b = *param_ptr++;
                    cfls[(size_t)i] = {a, b, 1.0f, 0.0f};
                }
            }
            centered_predictor = true;
            return true;
        };

        // Prefer legacy when byte size exactly matches historical stream.
        if ((size_t)sz_cfl == legacy_size) {
            parse_legacy();
            return;
        }
        if (try_parse_adaptive()) return;
        if ((size_t)sz_cfl % 2 == 0) {
            parse_legacy();
            return;
        }
        // Malformed/unknown: disable CfL for this tile.
        cfls.assign(nb_sz, {0.0f, 128.0f, 0.0f, 0.0f});
        centered_predictor = false;
    }

    static std::vector<uint8_t> decode_plane(
        const uint8_t* td, size_t ts, uint32_t pw, uint32_t ph,
        const uint16_t deq[64], const std::vector<uint8_t>* y_ref = nullptr,
        uint16_t file_version = FileHeader::VERSION
    ) {
        const bool has_band_cdf = file_version >= FileHeader::VERSION_BAND_GROUP_CDF;
        const uint8_t* ptr = td;

        std::vector<Token> dcs;
        std::vector<Token> acs;
        std::vector<Token> ac_low_tokens, ac_mid_tokens, ac_high_tokens;
        std::vector<int8_t> qds;
        std::vector<CfLParams> cfls;
        bool cfl_centered_predictor = false;
        uint32_t block_types_size = 0, palette_size = 0, copy_size = 0;

        const uint8_t* cfl_ptr = nullptr;
        uint32_t sz_cfl = 0;

        if (has_band_cdf) {
            // TileHeader v3 (lossy): 10 fields (40 bytes)
            uint32_t sz[10];
            std::memcpy(sz, td, 40);
            ptr = td + 40;

            dcs = decode_stream(ptr, sz[0]); ptr += sz[0];
            const uint8_t* low_ptr = ptr; ptr += sz[1];
            const uint8_t* mid_ptr = ptr; ptr += sz[2];
            const uint8_t* high_ptr = ptr; ptr += sz[3];
            const uint8_t* pindex_ptr = ptr;
            ptr += sz[4];

            BandPIndexBundle band_pi;
            bool has_band_pindex = false;
            if (sz[4] > 0) {
                has_band_pindex = parse_band_pindex_blob(
                    pindex_ptr, sz[4], sz[1], sz[2], sz[3], band_pi
                );
            }

            // Decode 3 AC bands in parallel (independent streams).
            auto f_low = std::async(std::launch::async, [=, &sz, &band_pi]() {
                if (has_band_pindex && band_pi.has_low) {
                    return decode_stream_parallel(low_ptr, sz[1], band_pi.low);
                }
                return decode_stream(low_ptr, sz[1]);
            });
            auto f_mid = std::async(std::launch::async, [=, &sz, &band_pi]() {
                if (has_band_pindex && band_pi.has_mid) {
                    return decode_stream_parallel(mid_ptr, sz[2], band_pi.mid);
                }
                return decode_stream(mid_ptr, sz[2]);
            });
            auto f_high = std::async(std::launch::async, [=, &sz, &band_pi]() {
                if (has_band_pindex && band_pi.has_high) {
                    return decode_stream_parallel(high_ptr, sz[3], band_pi.high);
                }
                return decode_stream(high_ptr, sz[3]);
            });
            ac_low_tokens = f_low.get();
            ac_mid_tokens = f_mid.get();
            ac_high_tokens = f_high.get();

            if (sz[5] > 0) { qds.resize(sz[5]); std::memcpy(qds.data(), ptr, sz[5]); ptr += sz[5]; }
            
            cfl_ptr = ptr;
            sz_cfl = sz[6];
            ptr += sz_cfl;

            block_types_size = sz[7];
            palette_size = sz[8];
            copy_size = sz[9];
        } else {
            // TileHeader v2 (legacy): 8 fields (32 bytes)
            uint32_t sz[8];
            std::memcpy(sz, td, 32);
            ptr = td + 32;

            dcs = decode_stream(ptr, sz[0]); ptr += sz[0];
            if (sz[2] > 0) {
                PIndex pi = PIndexCodec::deserialize(std::span<const uint8_t>(td + 32 + sz[0] + sz[1], sz[2]));
                acs = decode_stream_parallel(ptr, sz[1], pi);
            } else {
                acs = decode_stream(ptr, sz[1]);
            }
            ptr += sz[1] + sz[2];

            if (sz[3] > 0) { qds.resize(sz[3]); std::memcpy(qds.data(), ptr, sz[3]); ptr += sz[3]; }
            
            cfl_ptr = ptr;
            sz_cfl = sz[4];
            ptr += sz_cfl;

            block_types_size = sz[5];
            palette_size = sz[6];
            copy_size = sz[7];
        }

        int nx = pw/8, nb = nx*(ph/8); std::vector<uint8_t> pad(pw*ph);

        parse_cfl_stream(cfl_ptr, sz_cfl, nb, cfls, cfl_centered_predictor);

        std::vector<FileHeader::BlockType> block_types;
        if (block_types_size > 0) {
            block_types = decode_block_types(ptr, block_types_size, nb, file_version);
            ptr += block_types_size;
        } else {
            block_types.assign(nb, FileHeader::BlockType::DCT);
        }
        
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        if (palette_size > 0) {
            int num_palette_blocks = 0;
            for(auto t : block_types) if (t == FileHeader::BlockType::PALETTE) num_palette_blocks++;
            PaletteCodec::decode_palette_stream(ptr, palette_size, palettes, palette_indices, num_palette_blocks);
            ptr += palette_size;
        }

        std::vector<CopyParams> copy_params;
        if (copy_size > 0) {
            int num_copy = 0;
            for(auto t : block_types) if (t == FileHeader::BlockType::COPY) num_copy++;
            CopyCodec::decode_copy_stream(ptr, copy_size, copy_params, num_copy);
            ptr += copy_size;
        }
        
        std::vector<uint32_t> block_starts(nb + 1);
        std::vector<uint32_t> low_starts, mid_starts, high_starts;

        auto build_dct_block_starts = [&](const std::vector<Token>& tokens) {
            std::vector<uint32_t> starts(nb + 1);
            size_t cur = 0;
            for (int i = 0; i < nb; i++) {
                starts[i] = (uint32_t)cur;
                if (block_types[i] == FileHeader::BlockType::DCT) {
                    while (cur < tokens.size()) {
                        if (tokens[cur++].type == TokenType::ZRUN_63) break;
                        if (cur < tokens.size() && (int)tokens[cur - 1].type < 63) cur++; // skip MAGC
                    }
                }
            }
            starts[nb] = (uint32_t)cur;
            return starts;
        };

        if (has_band_cdf) {
            low_starts = build_dct_block_starts(ac_low_tokens);
            mid_starts = build_dct_block_starts(ac_mid_tokens);
            high_starts = build_dct_block_starts(ac_high_tokens);
        } else {
            block_starts = build_dct_block_starts(acs);
        }

        // Threading logic:
        // If Copy Mode is used, we force sequential decoding (nt=1)
        // to ensure Intra-Block Copy vectors point to already-decoded pixels.
        unsigned int nt = std::thread::hardware_concurrency(); if (nt == 0) nt = 4; nt = std::min<unsigned int>(nt, 8); nt = std::max(1u, std::min<unsigned int>(nt, (unsigned int)nb));
        if (copy_size > 0) {
            nt = 1;
        }

        std::vector<std::future<void>> futs; int bpt = nb / nt;
        for (unsigned int t = 0; t < nt; t++) {
            int sb = t * bpt, eb = (t == nt - 1) ? nb : (t + 1) * bpt;
            futs.push_back(std::async(std::launch::async, [=, &dcs, &acs, &ac_low_tokens, &ac_mid_tokens, &ac_high_tokens, &block_starts, &low_starts, &mid_starts, &high_starts, &qds, &cfls, &pad, &block_types, &palettes, &palette_indices, &copy_params]() {
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
                        std::fill(ac, ac + 63, 0);

                        if (has_band_cdf) {
                            size_t low_pos = low_starts[i];
                            size_t mid_pos = mid_starts[i];
                            size_t high_pos = high_starts[i];
                            detokenize_ac_band_block(ac_low_tokens, low_pos, BAND_LOW, ac);
                            detokenize_ac_band_block(ac_mid_tokens, mid_pos, BAND_MID, ac);
                            detokenize_ac_band_block(ac_high_tokens, high_pos, BAND_HIGH, ac);
                        } else {
                            uint32_t start = block_starts[i], end = block_starts[i+1];
                            int pos = 0;
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
                        }
                        float s = 1.0f; if (!qds.empty()) s = 1.0f + qds[i] / 50.0f;
                        int16_t dq[64]; dq[0] = dc * (uint16_t)std::max(1.0f, std::round(deq[0]*s));
                        for (int k = 1; k < 64; k++) dq[k] = ac[k-1] * (uint16_t)std::max(1.0f, std::round(deq[k]*s));
                        int16_t co[64], bl[64]; Zigzag::inverse_scan(dq, co); DCT::inverse(co, bl);
                        if (y_ref && !cfls.empty() && i < (int)cfls.size()) {
                            if (cfl_centered_predictor) {
                                if (cfls[i].alpha_cr > 0.5f) {
                                    int16_t a6 = (int16_t)std::round(cfls[i].alpha_cb * 64.0f);
                                    int16_t b = (int16_t)std::round(cfls[i].beta_cb);
                                    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                                        int py = (*y_ref)[(by*8+y)*pw+(bx*8+x)];
                                        int p = (a6 * (py - 128) + 32) >> 6;
                                        p += b;
                                        pad[(by*8+y)*pw+(bx*8+x)] = std::clamp((int)bl[y*8+x] + p, 0, 255);
                                    }
                                } else {
                                    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) pad[(by*8+y)*pw+(bx*8+x)] = (uint8_t)std::clamp(bl[y*8+x]+128, 0, 255);
                                }
                            } else {
                                float a = cfls[i].alpha_cb;
                                float b = cfls[i].beta_cb;
                                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                                    int py = (*y_ref)[(by*8+y)*pw+(bx*8+x)];
                                    int p = (int)std::lround(a * py + b);
                                    pad[(by*8+y)*pw+(bx*8+x)] = std::clamp((int)bl[y*8+x] + p, 0, 255);
                                }
                            }
                        } else {
                            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) pad[(by*8+y)*pw+(bx*8+x)] = (uint8_t)std::clamp(bl[y*8+x]+128, 0, 255);
                        }
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
    static std::vector<FileHeader::BlockType> decode_block_types(
        const uint8_t* val, size_t sz, int nb,
        uint16_t file_version = FileHeader::MIN_SUPPORTED_VERSION
    ) {
        std::vector<FileHeader::BlockType> out;
        out.reserve(nb);
        const uint8_t* runs = val;
        size_t runs_size = sz;

        // Optional compact envelope for v0.6+:
        // [BLOCK_MAGIC][mode][raw_count:u32][encoded_raw_runs]
        std::vector<uint8_t> decoded_runs;
        if (file_version >= FileHeader::VERSION_BLOCK_TYPES_V2 && sz >= 6) {
            bool is_v2 = (val[0] == 0xA6); // Or check WRAPPER_MAGIC_BLOCK_TYPES
            // But we might be using 0xA6 correctly. Let's use flexible check.
            if (val[0] == FileHeader::WRAPPER_MAGIC_BLOCK_TYPES) {
                uint8_t mode = val[1];
                uint32_t raw_count = 0;
                std::memcpy(&raw_count, val + 2, 4);
                const uint8_t* enc_ptr = val + 6;
                size_t enc_size = sz - 6;
                
                if (mode == 1) { // Mode 1: rANS
                    decoded_runs = decode_byte_stream(enc_ptr, enc_size, raw_count);
                } else if (mode == 2) { // Mode 2: LZ (Phase 9l-2)
                    decoded_runs = TileLZ::decompress(enc_ptr, enc_size, raw_count);
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
        auto plane = decode_plane_lossless(&hkn[t0->offset], t0->size, hdr.width, hdr.height, hdr.version);

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

        auto y_plane  = decode_plane_lossless(&hkn[t0->offset], t0->size, w, h, hdr.version);
        auto co_plane = decode_plane_lossless(&hkn[t1->offset], t1->size, w, h, hdr.version);
        auto cg_plane = decode_plane_lossless(&hkn[t2->offset], t2->size, w, h, hdr.version);

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
        const uint8_t* td, size_t ts, uint32_t width, uint32_t height,
        uint16_t file_version = FileHeader::VERSION
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
        uint32_t tile4_data_size = (file_version >= FileHeader::VERSION_TILE_MATCH4) ? hdr[7] : 0;

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
            block_types = decode_block_types(ptr, block_types_size, nb, file_version);
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
            const uint8_t* pal_ptr = ptr;
            size_t pal_size = palette_data_size;
            std::vector<uint8_t> pal_decoded;
            // Optional compact envelope for v0.6+:
            // [WRAPPER_MAGIC_PALETTE][mode][raw_count:u32][encoded_payload]
            if (file_version >= FileHeader::VERSION_BLOCK_TYPES_V2 &&
                palette_data_size >= 6 && ptr[0] == FileHeader::WRAPPER_MAGIC_PALETTE) {
                
                uint8_t mode = ptr[1];
                uint32_t raw_count = 0;
                std::memcpy(&raw_count, ptr + 2, 4);
                const uint8_t* enc_ptr = ptr + 6;
                size_t enc_size = palette_data_size - 6;
                
                if (mode == 1) {
                    pal_decoded = decode_byte_stream(enc_ptr, enc_size, raw_count);
                } else if (mode == 2) { // LZ
                    pal_decoded = TileLZ::decompress(enc_ptr, enc_size, raw_count);
                }

                if (!pal_decoded.empty()) {
                    pal_ptr = pal_decoded.data();
                    pal_size = pal_decoded.size();
                }
            }
            PaletteCodec::decode_palette_stream(pal_ptr, pal_size, palettes, palette_indices, num_palette);
            ptr += palette_data_size;
        }

        // Decode copy data
        std::vector<CopyParams> copy_params;
        if (copy_data_size > 0) {
            const uint8_t* cptr = ptr;
            size_t csz = copy_data_size;
            std::vector<uint8_t> unpacked;

            if (csz > 6 && cptr[0] == FileHeader::WRAPPER_MAGIC_COPY) {
                uint8_t mode = cptr[1];
                uint32_t raw_count;
                std::memcpy(&raw_count, cptr + 2, 4);
                
                if (mode == 2) { // LZ
                     if (TileLZ::decompress_to(cptr + 6, csz - 6, unpacked, raw_count)) {
                         cptr = unpacked.data();
                         csz = unpacked.size();
                     }
                }
            }

            int num_copy = 0;
            for (auto t : block_types) if (t == FileHeader::BlockType::COPY) num_copy++;
            CopyCodec::decode_copy_stream(cptr, csz, copy_params, num_copy);
            ptr += copy_data_size;
        }

        // Decode tile4 data
        struct Tile4Result { uint8_t indices[4]; };
        std::vector<Tile4Result> tile4_params;
        const uint8_t* end = td + ts;
        if (tile4_data_size > 0) {
            // Guard malformed stream: odd byte size or truncated payload.
            if ((tile4_data_size & 1u) != 0 || ptr > end || tile4_data_size > (uint32_t)(end - ptr)) {
                tile4_data_size = 0;
            }
        }
        if (tile4_data_size > 0) {
            for (uint32_t i = 0; i < tile4_data_size; i += 2) {
                Tile4Result res;
                res.indices[0] = ptr[i] & 0x0F;
                res.indices[1] = (ptr[i] >> 4) & 0x0F;
                res.indices[2] = ptr[i+1] & 0x0F;
                res.indices[3] = (ptr[i+1] >> 4) & 0x0F;
                tile4_params.push_back(res);
            }
            ptr += tile4_data_size;
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

        // Build per-block lookup for palette, copy, tile4
        std::vector<int> block_palette_idx(nb, -1);
        std::vector<int> block_copy_idx(nb, -1);
        std::vector<int> block_tile4_idx(nb, -1);
        int pi = 0, ci = 0, t4i = 0;
        for (int i = 0; i < nb; i++) {
            if (block_types[i] == FileHeader::BlockType::PALETTE) block_palette_idx[i] = pi++;
            else if (block_types[i] == FileHeader::BlockType::COPY) block_copy_idx[i] = ci++;
            else if (block_types[i] == FileHeader::BlockType::TILE_MATCH4) block_tile4_idx[i] = t4i++;
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

        // Process rows in raster order: Copy, Tile4, and Filter blocks
        const CopyParams kTileMatch4Candidates[16] = {
            CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
            CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
            CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
        };

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
                } else if (block_types[block_idx] == FileHeader::BlockType::TILE_MATCH4) {
                    int t4idx = block_tile4_idx[block_idx];
                    if (t4idx >= 0 && t4idx < (int)tile4_params.size()) {
                        int qx = ((int)x % 8) / 4;
                        int qy = ((int)y % 8) / 4;
                        int q = qy * 2 + qx;
                        int cand_idx = tile4_params[t4idx].indices[q];
                        int src_x = (int)x + kTileMatch4Candidates[cand_idx].dx;
                        int src_y = (int)y + kTileMatch4Candidates[cand_idx].dy;
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
                        case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
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
