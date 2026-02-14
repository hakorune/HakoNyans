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

