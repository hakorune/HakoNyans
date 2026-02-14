#pragma once

    // Lossless decoding
    // ========================================================================

    /**
     * Decode a lossless grayscale .hkn file.
     */
    static std::vector<uint8_t> decode_lossless(const std::vector<uint8_t>& hkn) {
        reset_lossless_decode_debug_stats();
        using Clock = std::chrono::steady_clock;
        const auto t_total0 = Clock::now();

        const auto t_hdr0 = Clock::now();
        FileHeader hdr = FileHeader::read(hkn.data());
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* t0 = dir.find("TIL0");
        const auto t_hdr1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_header_dir_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_hdr1 - t_hdr0).count();

        const auto t_plane0 = Clock::now();
        auto plane = decode_plane_lossless(&hkn[t0->offset], t0->size, hdr.width, hdr.height, hdr.version);
        const auto t_plane1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_plane_y_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane1 - t_plane0).count();

        // int16_t -> uint8_t
        std::vector<uint8_t> out(hdr.width * hdr.height);
        for (size_t i = 0; i < out.size(); i++) {
            out[i] = (uint8_t)std::clamp((int)plane[i], 0, 255);
        }
        const auto t_total1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_color_total_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_total1 - t_total0).count();
        return out;
    }

    /**
     * Decode a lossless color .hkn file (YCoCg-R).
     */
    static std::vector<uint8_t> decode_color_lossless(const std::vector<uint8_t>& hkn, int& w, int& h) {
        reset_lossless_decode_debug_stats();
        using Clock = std::chrono::steady_clock;
        const auto t_total0 = Clock::now();

        const auto t_hdr0 = Clock::now();
        FileHeader hdr = FileHeader::read(hkn.data());
        w = hdr.width; h = hdr.height;
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
        const ChunkEntry* t0 = dir.find("TIL0");
        const ChunkEntry* t1 = dir.find("TIL1");
        const ChunkEntry* t2 = dir.find("TIL2");
        const auto t_hdr1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_header_dir_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_hdr1 - t_hdr0).count();

        struct PlaneDecodeTaskResult {
            std::vector<int16_t> plane;
            LosslessDecodeDebugStats stats;
            uint64_t elapsed_ns = 0;
        };

        auto run_plane_task = [&](size_t offset, size_t size, bool reset_task_stats) -> PlaneDecodeTaskResult {
            using TaskClock = std::chrono::steady_clock;
            if (reset_task_stats) GrayscaleDecoder::reset_lossless_decode_debug_stats();
            const auto t0p = TaskClock::now();
            auto plane = GrayscaleDecoder::decode_plane_lossless(&hkn[offset], size, w, h, hdr.version);
            const auto t1p = TaskClock::now();

            PlaneDecodeTaskResult out;
            out.plane = std::move(plane);
            if (reset_task_stats) {
                out.stats = GrayscaleDecoder::get_lossless_decode_debug_stats();
            }
            out.elapsed_ns =
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1p - t0p).count();
            return out;
        };

        std::vector<int16_t> y_plane, co_plane, cg_plane;
        const unsigned int hw_threads = thread_budget::max_threads();
        ThreadPool& worker_pool = decode_worker_pool();
        auto submit_plane_task = [&](size_t offset, size_t size, bool reset_task_stats) {
            return worker_pool.submit([&, offset, size, reset_task_stats]() {
                thread_budget::ScopedParallelRegion guard;
                return run_plane_task(offset, size, reset_task_stats);
            });
        };
        const auto t_plane_dispatch0 = Clock::now();
        auto plane_decode_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(3);
        if (plane_decode_tokens.acquired()) {
            tl_lossless_decode_debug_stats_.decode_plane_parallel_3way_count++;
            tl_lossless_decode_debug_stats_.decode_plane_parallel_tokens_sum +=
                (uint64_t)plane_decode_tokens.count();
        } else {
            tl_lossless_decode_debug_stats_.decode_plane_parallel_seq_count++;
        }
        const auto t_plane_dispatch1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_plane_dispatch_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                t_plane_dispatch1 - t_plane_dispatch0
            ).count();

        if (plane_decode_tokens.acquired()) {
            if (decode_use_plane_caller_y_path()) {
                auto fco = submit_plane_task(t1->offset, t1->size, true);
                auto fcg = submit_plane_task(t2->offset, t2->size, true);

                PlaneDecodeTaskResult y_res;
                {
                    thread_budget::ScopedParallelRegion guard;
                    y_res = run_plane_task(t0->offset, t0->size, false);
                }

                const auto t_plane_wait0 = Clock::now();
                auto co_res = fco.get();
                auto cg_res = fcg.get();
                const auto t_plane_wait1 = Clock::now();
                tl_lossless_decode_debug_stats_.decode_plane_wait_ns +=
                    (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                        t_plane_wait1 - t_plane_wait0
                    ).count();

                y_plane = std::move(y_res.plane);
                co_plane = std::move(co_res.plane);
                cg_plane = std::move(cg_res.plane);

                tl_lossless_decode_debug_stats_.accumulate_from(co_res.stats);
                tl_lossless_decode_debug_stats_.accumulate_from(cg_res.stats);

                tl_lossless_decode_debug_stats_.decode_plane_y_ns += y_res.elapsed_ns;
                tl_lossless_decode_debug_stats_.decode_plane_co_ns += co_res.elapsed_ns;
                tl_lossless_decode_debug_stats_.decode_plane_cg_ns += cg_res.elapsed_ns;
            } else {
                auto fy = submit_plane_task(t0->offset, t0->size, true);
                auto fco = submit_plane_task(t1->offset, t1->size, true);
                auto fcg = submit_plane_task(t2->offset, t2->size, true);

                const auto t_plane_wait0 = Clock::now();
                auto y_res = fy.get();
                auto co_res = fco.get();
                auto cg_res = fcg.get();
                const auto t_plane_wait1 = Clock::now();
                tl_lossless_decode_debug_stats_.decode_plane_wait_ns +=
                    (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                        t_plane_wait1 - t_plane_wait0
                    ).count();

                y_plane = std::move(y_res.plane);
                co_plane = std::move(co_res.plane);
                cg_plane = std::move(cg_res.plane);

                tl_lossless_decode_debug_stats_.accumulate_from(y_res.stats);
                tl_lossless_decode_debug_stats_.accumulate_from(co_res.stats);
                tl_lossless_decode_debug_stats_.accumulate_from(cg_res.stats);

                tl_lossless_decode_debug_stats_.decode_plane_y_ns += y_res.elapsed_ns;
                tl_lossless_decode_debug_stats_.decode_plane_co_ns += co_res.elapsed_ns;
                tl_lossless_decode_debug_stats_.decode_plane_cg_ns += cg_res.elapsed_ns;
            }
        } else {
            const auto t_y0 = Clock::now();
            y_plane  = decode_plane_lossless(&hkn[t0->offset], t0->size, w, h, hdr.version);
            const auto t_y1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_plane_y_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_y1 - t_y0).count();

            const auto t_co0 = Clock::now();
            co_plane = decode_plane_lossless(&hkn[t1->offset], t1->size, w, h, hdr.version);
            const auto t_co1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_plane_co_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_co1 - t_co0).count();

            const auto t_cg0 = Clock::now();
            cg_plane = decode_plane_lossless(&hkn[t2->offset], t2->size, w, h, hdr.version);
            const auto t_cg1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_plane_cg_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_cg1 - t_cg0).count();
        }

        // YCoCg-R -> RGB
        const auto t_rgb0 = Clock::now();
        std::vector<uint8_t> rgb(w * h * 3);
        unsigned int rgb_threads =
            std::max(1u, std::min<unsigned int>(hw_threads, (unsigned int)h));
        const uint64_t pixel_count = (uint64_t)w * (uint64_t)h;
        constexpr unsigned int kMaxRgbThreads = 8;
        constexpr unsigned int kMinRowsPerTask = 128;
        constexpr uint64_t kMinPixelsPerTask = 200000; // Avoid over-sharding small frames.
        rgb_threads = std::min(rgb_threads, kMaxRgbThreads);
        if ((unsigned int)h > 0) {
            const unsigned int by_rows =
                std::max(1u, (unsigned int)h / kMinRowsPerTask);
            rgb_threads = std::min(rgb_threads, by_rows);
        }
        if (pixel_count > 0) {
            const unsigned int by_pixels =
                std::max(1u, (unsigned int)(pixel_count / kMinPixelsPerTask));
            rgb_threads = std::min(rgb_threads, by_pixels);
        }
        const auto t_rgb_dispatch0 = Clock::now();
        thread_budget::ScopedThreadTokens ycocg_to_rgb_tokens;
        if (rgb_threads > 1) {
            ycocg_to_rgb_tokens = thread_budget::ScopedThreadTokens::try_acquire_up_to(
                rgb_threads, 2
            );
            if (ycocg_to_rgb_tokens.acquired()) {
                rgb_threads = ycocg_to_rgb_tokens.count();
            } else {
                rgb_threads = 1;
            }
        } else {
            rgb_threads = 1;
        }
        if (rgb_threads > 1) {
            tl_lossless_decode_debug_stats_.decode_ycocg_parallel_count++;
            tl_lossless_decode_debug_stats_.decode_ycocg_parallel_threads_sum += rgb_threads;
            struct YcocgTaskResult {
                uint64_t kernel_ns = 0;
                uint64_t rows = 0;
                uint64_t pixels = 0;
            };
            auto run_rows = [&](int sy, int ey) -> YcocgTaskResult {
                const auto t_kernel0 = Clock::now();
                for (int y = sy; y < ey; y++) {
                    const int row_off = y * w;
                    for (int x = 0; x < w; x++) {
                        const int i = row_off + x;
                        ycocg_r_to_rgb(y_plane[i], co_plane[i], cg_plane[i],
                                       rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
                    }
                }
                const auto t_kernel1 = Clock::now();
                YcocgTaskResult out;
                out.kernel_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_kernel1 - t_kernel0
                ).count();
                out.rows = (uint64_t)(ey - sy);
                out.pixels = (uint64_t)(ey - sy) * (uint64_t)w;
                return out;
            };
            std::vector<std::future<YcocgTaskResult>> futs;
            futs.reserve((rgb_threads > 0) ? (rgb_threads - 1) : 0);
            for (unsigned int t = 1; t < rgb_threads; t++) {
                const int sy = (int)(((uint64_t)t * (uint64_t)h) / (uint64_t)rgb_threads);
                const int ey = (int)(((uint64_t)(t + 1) * (uint64_t)h) / (uint64_t)rgb_threads);
                futs.push_back(worker_pool.submit([&, sy, ey]() {
                    thread_budget::ScopedParallelRegion guard;
                    return run_rows(sy, ey);
                }));
            }
            const auto t_rgb_dispatch1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_ycocg_dispatch_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_rgb_dispatch1 - t_rgb_dispatch0
                ).count();
            const int sy0 = 0;
            const int ey0 = (int)(((uint64_t)1 * (uint64_t)h) / (uint64_t)rgb_threads);
            auto main_res = run_rows(sy0, ey0);
            tl_lossless_decode_debug_stats_.decode_ycocg_kernel_ns += main_res.kernel_ns;
            tl_lossless_decode_debug_stats_.decode_ycocg_rows_sum += main_res.rows;
            tl_lossless_decode_debug_stats_.decode_ycocg_pixels_sum += main_res.pixels;
            const auto t_rgb_wait0 = Clock::now();
            for (auto& f : futs) {
                auto r = f.get();
                tl_lossless_decode_debug_stats_.decode_ycocg_kernel_ns += r.kernel_ns;
                tl_lossless_decode_debug_stats_.decode_ycocg_rows_sum += r.rows;
                tl_lossless_decode_debug_stats_.decode_ycocg_pixels_sum += r.pixels;
            }
            const auto t_rgb_wait1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_ycocg_wait_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_rgb_wait1 - t_rgb_wait0
                ).count();
        } else {
            const auto t_rgb_dispatch1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_ycocg_dispatch_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_rgb_dispatch1 - t_rgb_dispatch0
                ).count();
            tl_lossless_decode_debug_stats_.decode_ycocg_sequential_count++;
            const auto t_rgb_kernel0 = Clock::now();
            for (int i = 0; i < w * h; i++) {
                ycocg_r_to_rgb(y_plane[i], co_plane[i], cg_plane[i],
                               rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
            }
            const auto t_rgb_kernel1 = Clock::now();
            tl_lossless_decode_debug_stats_.decode_ycocg_kernel_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_rgb_kernel1 - t_rgb_kernel0
                ).count();
            tl_lossless_decode_debug_stats_.decode_ycocg_rows_sum += (uint64_t)h;
            tl_lossless_decode_debug_stats_.decode_ycocg_pixels_sum +=
                (uint64_t)w * (uint64_t)h;
        }
        const auto t_rgb1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_ycocg_to_rgb_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_rgb1 - t_rgb0).count();
        tl_lossless_decode_debug_stats_.decode_color_total_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_rgb1 - t_total0).count();
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
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        auto out = lossless_plane_decode_core::decode_plane_lossless(
            td,
            ts,
            width,
            height,
            file_version,
            [](const uint8_t* data, size_t size, size_t raw_count) {
                return GrayscaleDecoder::decode_byte_stream(data, size, raw_count);
            },
            [](const uint8_t* data, size_t size, size_t raw_count) {
                return GrayscaleDecoder::decode_byte_stream_shared_lz(data, size, raw_count);
            },
            &tl_lossless_decode_debug_stats_
        );
        const auto t1 = Clock::now();
        tl_lossless_decode_debug_stats_.decode_plane_total_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        tl_lossless_decode_debug_stats_.decode_plane_calls++;
        return out;
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
        if ((cdf_size & 3u) != 0u) return std::vector<uint8_t>(expected_count, 0);
        if ((size_t)cdf_size > size - 12) return std::vector<uint8_t>(expected_count, 0);

        CDFTable cdf{};
        bool cdf_owned = false;
        if (!try_build_cdf_from_serialized_freq(data + 4, cdf_size, cdf)) {
            std::vector<uint32_t> freq(cdf_size / 4u);
            std::memcpy(freq.data(), data + 4, cdf_size);
            cdf = CDFBuilder().build_from_freq(freq);
            cdf_owned = true;
        }

        uint32_t count;
        std::memcpy(&count, data + 4 + cdf_size, 4);

        uint32_t rans_size;
        std::memcpy(&rans_size, data + 8 + cdf_size, 4);
        if ((size_t)rans_size > size - 12 - (size_t)cdf_size) {
            return std::vector<uint8_t>(expected_count, 0);
        }

        FlatInterleavedDecoder dec(std::span<const uint8_t>(data + 12 + cdf_size, rans_size));
        std::vector<uint8_t> result(count);
        if (count > 0) {
            uint8_t* dst = result.data();
            constexpr uint32_t kUseLutMinCount = 128;
            const bool use_bulk = decode_use_bulk_rans();
            if (count >= kUseLutMinCount) {
                thread_local SIMDDecodeTable tbl;
                build_simd_table_inplace(cdf, tbl);
                if (use_bulk) {
                    dec.decode_symbols_lut(dst, count, tbl);
                } else {
                    for (uint32_t i = 0; i < count; i++) {
                        dst[i] = (uint8_t)dec.decode_symbol_lut(tbl);
                    }
                }
            } else {
                if (use_bulk) {
                    dec.decode_symbols(dst, count, cdf);
                } else {
                    for (uint32_t i = 0; i < count; i++) {
                        dst[i] = (uint8_t)dec.decode_symbol(cdf);
                    }
                }
            }
        }
        if (cdf_owned) CDFBuilder::cleanup(cdf);
        if (expected_count > 0 && result.size() != expected_count) {
            result.resize(expected_count, 0);
        }
        return result;
    }

    // Shared/static-CDF variant for Mode5 payload.
    // Format: [4B count][4B rans_size][rans_data]
    static std::vector<uint8_t> decode_byte_stream_shared_lz(
        const uint8_t* data, size_t size, size_t expected_count
    ) {
        if (size < 8) return std::vector<uint8_t>(expected_count, 0);

        uint32_t count = 0;
        uint32_t rans_size = 0;
        std::memcpy(&count, data, 4);
        std::memcpy(&rans_size, data + 4, 4);

        if ((size_t)rans_size > size - 8) {
            return std::vector<uint8_t>(expected_count, 0);
        }

        const CDFTable& cdf = get_mode5_shared_lz_cdf();
        FlatInterleavedDecoder dec(std::span<const uint8_t>(data + 8, rans_size));
        std::vector<uint8_t> result(count);
        if (count > 0) {
            const SIMDDecodeTable& tbl = get_mode5_shared_lz_simd_table();
            if (decode_use_bulk_rans()) {
                dec.decode_symbols_lut(result.data(), count, tbl);
            } else {
                for (uint32_t i = 0; i < count; i++) {
                    result[i] = (uint8_t)dec.decode_symbol_lut(tbl);
                }
            }
        }
        if (expected_count > 0 && result.size() != expected_count) {
            result.resize(expected_count, 0);
        }
        return result;
    }

