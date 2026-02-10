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

private:
    static std::vector<uint8_t> decode_plane(const uint8_t* td, size_t ts, uint32_t pw, uint32_t ph, const uint16_t deq[64], const std::vector<uint8_t>* y_ref = nullptr) {
        uint32_t sz[5]; std::memcpy(sz, td, 20); const uint8_t* ptr = td + 20;
        auto dcs = decode_stream(ptr, sz[0]); ptr += sz[0];
        std::vector<Token> acs;
        if (sz[2] > 0) {
            PIndex pi = PIndexCodec::deserialize(std::span<const uint8_t>(td + 20 + sz[0] + sz[1], sz[2]));
            acs = decode_stream_parallel(ptr, sz[1], pi);
        } else {
            acs = decode_stream(ptr, sz[1]);
        }
        ptr += sz[1] + sz[2];
        std::vector<int8_t> qds; if (sz[3] > 0) { qds.resize(sz[3]); std::memcpy(qds.data(), ptr, sz[3]); ptr += sz[3]; }
        std::vector<CfLParams> cfls; if (sz[4] > 0) { for (uint32_t i=0; i<sz[4]/2; i++) { float a = (int8_t)ptr[i*2]/64.0f, b = ptr[i*2+1]; cfls.push_back({a, b, a, b}); } }
        
        int nx = pw/8, nb = nx*(ph/8); std::vector<uint8_t> pad(pw*ph);
        
        std::vector<uint32_t> block_starts(nb + 1);
        size_t cur = 0;
        for (int i = 0; i < nb; i++) {
            block_starts[i] = (uint32_t)cur;
            while (cur < acs.size()) {
                if (acs[cur++].type == TokenType::ZRUN_63) break;
                if (cur < acs.size() && (int)acs[cur-1].type < 64) cur++; // Skip MAGC
            }
        }
        block_starts[nb] = (uint32_t)cur;

        unsigned int nt = std::thread::hardware_concurrency(); if (nt == 0) nt = 4; nt = std::min<unsigned int>(nt, 8); nt = std::max(1u, std::min<unsigned int>(nt, (unsigned int)nb));
        std::vector<std::future<void>> futs; int bpt = nb / nt;
        for (unsigned int t = 0; t < nt; t++) {
            int sb = t * bpt, eb = (t == nt - 1) ? nb : (t + 1) * bpt;
            futs.push_back(std::async(std::launch::async, [=, &dcs, &acs, &block_starts, &qds, &cfls, &pad, deq, y_ref]() {
                int16_t pdc = 0; for (int i = 0; i < sb; i++) pdc += Tokenizer::detokenize_dc(dcs[i]);
                int16_t ac[63];
                for (int i = sb; i < eb; i++) {
                    int bx = i % nx, by = i / nx; int16_t dc = pdc + Tokenizer::detokenize_dc(dcs[i]); pdc = dc;
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
                }
            }));
        }
        for (auto& f : futs) f.get(); return pad;
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
};

} // namespace hakonyans
