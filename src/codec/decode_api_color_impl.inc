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
        std::vector<uint8_t> cb_raw, cr_raw;
        auto chroma_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(2);
        if (chroma_tokens.acquired()) {
            auto f1 = std::async(std::launch::async, [=, &hkn, &y_ref]() {
                thread_budget::ScopedParallelRegion guard;
                return decode_plane(&hkn[t1->offset], t1->size, pcw, pch, deq_cb, is_cfl ? &y_ref : nullptr, hdr.version);
            });
            auto f2 = std::async(std::launch::async, [=, &hkn, &y_ref]() {
                thread_budget::ScopedParallelRegion guard;
                return decode_plane(&hkn[t2->offset], t2->size, pcw, pch, deq_cr, is_cfl ? &y_ref : nullptr, hdr.version);
            });
            cb_raw = f1.get();
            cr_raw = f2.get();
        } else {
            cb_raw = decode_plane(&hkn[t1->offset], t1->size, pcw, pch, deq_cb, is_cfl ? &y_ref : nullptr, hdr.version);
            cr_raw = decode_plane(&hkn[t2->offset], t2->size, pcw, pch, deq_cr, is_cfl ? &y_ref : nullptr, hdr.version);
        }
        std::vector<uint8_t> y_p(w * h), cb_p(w * h), cr_p(w * h);
        for (int y = 0; y < h; y++) std::memcpy(&y_p[y * w], &yp_v[y * pyw], w);
        if (is_420) {
            std::vector<uint8_t> cbc(cw * ch), crc(cw * ch);
            for (int y = 0; y < ch; y++) { std::memcpy(&cbc[y * cw], &cb_raw[y * pcw], cw); std::memcpy(&crc[y * cw], &cr_raw[y * pcw], cw); }
            upsample_420_bilinear(cbc.data(), cw, ch, cb_p, w, h); upsample_420_bilinear(crc.data(), cw, ch, cr_p, w, h);
        } else {
            for (int y = 0; y < h; y++) { std::memcpy(&cb_p[y * w], &cb_raw[y * pyw], w); std::memcpy(&cr_p[y * w], &cr_raw[y * pyw], w); }
        }
        std::vector<uint8_t> rgb(w * h * 3);
        unsigned int nt = thread_budget::max_threads(8);
        nt = std::max(1u, std::min<unsigned int>(nt, (unsigned int)h));
        auto ycbcr_to_rgb_tokens = thread_budget::ScopedThreadTokens::try_acquire_up_to(nt, 2);
        if (ycbcr_to_rgb_tokens.acquired()) {
            nt = ycbcr_to_rgb_tokens.count();
            std::vector<std::future<void>> futs;
            int rpt = h / (int)nt;
            for (unsigned int t = 0; t < nt; t++) {
                int sy = (int)t * rpt, ey = (t == nt - 1) ? h : (int)(t + 1) * rpt;
                futs.push_back(std::async(std::launch::async, [=, &y_p, &cb_p, &cr_p, &rgb]() {
                    thread_budget::ScopedParallelRegion guard;
                    for (int y = sy; y < ey; y++) {
                        simd::ycbcr_to_rgb_row(&y_p[y*w], &cb_p[y*w], &cr_p[y*w], &rgb[y*w*3], w);
                    }
                }));
            }
            for (auto& f : futs) f.get();
        } else {
            for (int y = 0; y < h; y++) {
                simd::ycbcr_to_rgb_row(&y_p[y*w], &cb_p[y*w], &cr_p[y*w], &rgb[y*w*3], w);
            }
        }
        return rgb;
    }
