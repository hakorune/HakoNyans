#pragma once

    static std::vector<uint8_t> encode_lossless(
        const uint8_t* pixels, uint32_t width, uint32_t height,
        LosslessPreset preset = LosslessPreset::BALANCED
    ) {
        reset_lossless_mode_debug_stats();
        using Clock = std::chrono::steady_clock;
        const auto t_total0 = Clock::now();

        FileHeader header;
        header.width = width; header.height = height;
        header.bit_depth = 8; header.num_channels = 1;
        header.colorspace = 2; // RGB (grayscale)
        header.subsampling = 0; header.tile_cols = 1; header.tile_rows = 1;
        header.quality = 0;    // 0 = lossless
        header.flags |= 1;    // bit0 = lossless
        header.pindex_density = 0;

        // Convert to int16_t plane
        std::vector<int16_t> plane(width * height);
        for (uint32_t i = 0; i < width * height; i++) {
            plane[i] = (int16_t)pixels[i];
        }

        const auto t_cls0 = Clock::now();
        auto profile = classify_lossless_profile(plane.data(), width, height);
        const auto t_cls1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_profile_classify_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_cls1 - t_cls0).count();

        auto preset_plan = build_lossless_preset_plan(preset, profile);
        const auto t_plane0 = Clock::now();
        auto tile_data = encode_plane_lossless(
            plane.data(),
            width,
            height,
            profile,
            preset_plan.route_compete_luma,
            false,
            preset_plan.natural_route_mode2_nice_length_override,
            preset_plan.natural_route_mode2_match_strategy_override,
            preset_plan.filter_row_cost_model,
            preset_plan.filter_lo_lz_probe_enable
        );
        const auto t_plane1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_y_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane1 - t_plane0).count();

        // Build file: Header + ChunkDir + Tile
        const auto t_pack0 = Clock::now();
        ChunkDirectory dir;
        dir.add("TIL0", 0, tile_data.size());
        auto dir_data = dir.serialize();
        size_t tile_offset = 48 + dir_data.size();
        dir.entries[0].offset = tile_offset;
        dir_data = dir.serialize();

        std::vector<uint8_t> output;
        output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        const auto t_pack1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_container_pack_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_pack0).count();
        tl_lossless_mode_debug_stats_.perf_encode_total_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_total0).count();
        return output;
    }

    /**
     * Encode a color image losslessly using YCoCg-R.
     */
    static std::vector<uint8_t> encode_color_lossless(
        const uint8_t* rgb_data, uint32_t width, uint32_t height,
        LosslessPreset preset = LosslessPreset::BALANCED
    ) {
        reset_lossless_mode_debug_stats();
        using Clock = std::chrono::steady_clock;
        const auto t_total0 = Clock::now();

        // RGB -> YCoCg-R
        std::vector<int16_t> y_plane(width * height);
        std::vector<int16_t> co_plane(width * height);
        std::vector<int16_t> cg_plane(width * height);

        const auto t_rgb0 = Clock::now();
        for (uint32_t i = 0; i < width * height; i++) {
            rgb_to_ycocg_r(rgb_data[i * 3], rgb_data[i * 3 + 1], rgb_data[i * 3 + 2],
                            y_plane[i], co_plane[i], cg_plane[i]);
        }
        const auto t_rgb1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_rgb_to_ycocg_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_rgb1 - t_rgb0).count();

        const auto t_cls0 = Clock::now();
        auto profile = classify_lossless_profile(y_plane.data(), width, height);
        const auto t_cls1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_profile_classify_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_cls1 - t_cls0).count();

        struct PlaneEncodeTaskResult {
            std::vector<uint8_t> tile;
            LosslessModeDebugStats stats;
            uint64_t elapsed_ns = 0;
        };

        auto preset_plan = build_lossless_preset_plan(preset, profile);
        const bool enable_y_route_compete = preset_plan.route_compete_luma;
        const bool allow_chroma_route_compete = preset_plan.route_compete_chroma;
        const bool conservative_chroma_route_policy = preset_plan.conservative_chroma_route_policy;
        const int natural_route_mode2_nice_length_override =
            preset_plan.natural_route_mode2_nice_length_override;
        const int natural_route_mode2_match_strategy_override =
            preset_plan.natural_route_mode2_match_strategy_override;
        const auto filter_row_cost_model = preset_plan.filter_row_cost_model;
        const bool filter_lo_lz_probe_enable = preset_plan.filter_lo_lz_probe_enable;
        auto run_plane_task = [
            width,
            height,
            profile,
            natural_route_mode2_nice_length_override,
            natural_route_mode2_match_strategy_override,
            filter_row_cost_model,
            filter_lo_lz_probe_enable
        ](
            const int16_t* plane, bool enable_route_compete, bool conservative_chroma_policy
        ) -> PlaneEncodeTaskResult {
            using TaskClock = std::chrono::steady_clock;
            GrayscaleEncoder::reset_lossless_mode_debug_stats();
            const auto t0 = TaskClock::now();
            auto tile = GrayscaleEncoder::encode_plane_lossless(
                plane,
                width,
                height,
                profile,
                enable_route_compete,
                conservative_chroma_policy,
                natural_route_mode2_nice_length_override,
                natural_route_mode2_match_strategy_override,
                filter_row_cost_model,
                filter_lo_lz_probe_enable
            );
            const auto t1 = TaskClock::now();

            PlaneEncodeTaskResult out;
            out.tile = std::move(tile);
            out.stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
            out.elapsed_ns =
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            return out;
        };

        std::vector<uint8_t> tile_y, tile_co, tile_cg;
        auto plane_tokens = thread_budget::ScopedThreadTokens::try_acquire_up_to(3, 2);
        if (plane_tokens.acquired()) {
            tl_lossless_mode_debug_stats_.perf_encode_plane_parallel_tokens_sum +=
                (uint64_t)plane_tokens.count();
            if (plane_tokens.count() >= 3) {
                tl_lossless_mode_debug_stats_.perf_encode_plane_parallel_3way_count++;
            } else {
                tl_lossless_mode_debug_stats_.perf_encode_plane_parallel_2way_count++;
            }
        } else {
            tl_lossless_mode_debug_stats_.perf_encode_plane_parallel_seq_count++;
        }
        if (plane_tokens.acquired()) {
            auto fy = std::async(std::launch::async, [&]() {
                thread_budget::ScopedParallelRegion guard;
                return run_plane_task(y_plane.data(), enable_y_route_compete, false);
            });
            auto fco = std::async(std::launch::async, [&]() {
                thread_budget::ScopedParallelRegion guard;
                return run_plane_task(
                    co_plane.data(), allow_chroma_route_compete, conservative_chroma_route_policy
                );
            });

            PlaneEncodeTaskResult y_res;
            PlaneEncodeTaskResult co_res;
            PlaneEncodeTaskResult cg_res;
            if (plane_tokens.count() >= 3) {
                auto fcg = std::async(std::launch::async, [&]() {
                    thread_budget::ScopedParallelRegion guard;
                    return run_plane_task(
                        cg_plane.data(), allow_chroma_route_compete, conservative_chroma_route_policy
                    );
                });
                y_res = fy.get();
                co_res = fco.get();
                cg_res = fcg.get();
            } else {
                cg_res = run_plane_task(
                    cg_plane.data(), allow_chroma_route_compete, conservative_chroma_route_policy
                );
                y_res = fy.get();
                co_res = fco.get();
            }

            tile_y = std::move(y_res.tile);
            tile_co = std::move(co_res.tile);
            tile_cg = std::move(cg_res.tile);

            tl_lossless_mode_debug_stats_.accumulate_from(y_res.stats);
            tl_lossless_mode_debug_stats_.accumulate_from(co_res.stats);
            tl_lossless_mode_debug_stats_.accumulate_from(cg_res.stats);

            tl_lossless_mode_debug_stats_.perf_encode_plane_y_ns += y_res.elapsed_ns;
            tl_lossless_mode_debug_stats_.perf_encode_plane_co_ns += co_res.elapsed_ns;
            tl_lossless_mode_debug_stats_.perf_encode_plane_cg_ns += cg_res.elapsed_ns;
        } else {
            const auto t_plane_y0 = Clock::now();
            tile_y = encode_plane_lossless(
                y_plane.data(),
                width,
                height,
                profile,
                enable_y_route_compete,
                false,
                natural_route_mode2_nice_length_override,
                natural_route_mode2_match_strategy_override,
                filter_row_cost_model,
                filter_lo_lz_probe_enable
            );
            const auto t_plane_y1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_y_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane_y1 - t_plane_y0).count();

            const auto t_plane_co0 = Clock::now();
            tile_co = encode_plane_lossless(
                co_plane.data(),
                width,
                height,
                profile,
                allow_chroma_route_compete,
                conservative_chroma_route_policy,
                natural_route_mode2_nice_length_override,
                natural_route_mode2_match_strategy_override,
                filter_row_cost_model,
                filter_lo_lz_probe_enable
            );
            const auto t_plane_co1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_co_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane_co1 - t_plane_co0).count();

            const auto t_plane_cg0 = Clock::now();
            tile_cg = encode_plane_lossless(
                cg_plane.data(),
                width,
                height,
                profile,
                allow_chroma_route_compete,
                conservative_chroma_route_policy,
                natural_route_mode2_nice_length_override,
                natural_route_mode2_match_strategy_override,
                filter_row_cost_model,
                filter_lo_lz_probe_enable
            );
            const auto t_plane_cg1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_cg_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane_cg1 - t_plane_cg0).count();
        }

        const auto t_pack0 = Clock::now();
        FileHeader header;
        header.width = width; header.height = height;
        header.bit_depth = 8; header.num_channels = 3;
        header.colorspace = 1; // YCoCg-R
        header.subsampling = 0; // 4:4:4 (no subsampling for lossless)
        header.tile_cols = 1; header.tile_rows = 1;
        header.quality = 0;
        header.flags |= 1;
        header.pindex_density = 0;

        ChunkDirectory dir;
        dir.add("TIL0", 0, tile_y.size());
        dir.add("TIL1", 0, tile_co.size());
        dir.add("TIL2", 0, tile_cg.size());
        auto dir_data = dir.serialize();

        size_t offset = 48 + dir_data.size();
        dir.entries[0].offset = offset; offset += tile_y.size();
        dir.entries[1].offset = offset; offset += tile_co.size();
        dir.entries[2].offset = offset;
        dir_data = dir.serialize();

        std::vector<uint8_t> output;
        output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), tile_y.begin(), tile_y.end());
        output.insert(output.end(), tile_co.begin(), tile_co.end());
        output.insert(output.end(), tile_cg.begin(), tile_cg.end());
        const auto t_pack1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_container_pack_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_pack0).count();
        tl_lossless_mode_debug_stats_.perf_encode_total_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_total0).count();
        return output;
    }

