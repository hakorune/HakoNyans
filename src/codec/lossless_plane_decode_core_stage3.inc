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
                    int16_t left = (x_base > 0) ? dst[-1] : 0;
                    int16_t up_left = (up && x_base > 0) ? up[-1] : 0;
                    for (size_t px = 0; px < kRun; px++) {
                        const int16_t b = up ? up[px] : 0;
                        const int16_t pred = LosslessFilter::predict(ftype, left, b, up_left);
                        const int16_t cur = (int16_t)((uint16_t)pred + (uint16_t)rs[px]);
                        dst[px] = cur;
                        left = cur;
                        up_left = b;
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
                    int16_t pred = LosslessFilter::predict(ftype, a, b, c);
                    if (residual_idx < residual_size) {
                        int16_t actual_residual = filter_residuals[residual_idx++];
                        padded[pos] = (int16_t)((uint16_t)pred + (uint16_t)actual_residual);
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
