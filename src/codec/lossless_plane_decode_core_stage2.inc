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
        file_version,
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

