    static int estimate_copy_bits(const CopyParams& cp, int tile_width, LosslessProfile profile) {
        return lossless_mode_select::estimate_copy_bits(cp, tile_width, static_cast<int>(profile));
    }

    static int estimate_palette_index_bits_per_pixel(int palette_size) {
        return lossless_mode_select::estimate_palette_index_bits_per_pixel(palette_size);
    }

    static int estimate_palette_bits(const Palette& p, int transitions, LosslessProfile profile) {
        return lossless_mode_select::estimate_palette_bits(p, transitions, static_cast<int>(profile));
    }

    static int estimate_filter_symbol_bits2(int abs_residual, LosslessProfile profile) {
        return lossless_mode_select::estimate_filter_symbol_bits2(abs_residual, static_cast<int>(profile));
    }

    static int lossless_filter_candidates(LosslessProfile profile) {
        return lossless_mode_select::lossless_filter_candidates(static_cast<int>(profile));
    }

    static int estimate_filter_bits(
        const int16_t* padded, uint32_t pad_w, uint32_t pad_h, int cur_x, int cur_y, LosslessProfile profile
    ) {
        return lossless_mode_select::estimate_filter_bits(
            padded, pad_w, pad_h, cur_x, cur_y, static_cast<int>(profile)
        );
    }

    using ScreenPreflightMetrics = lossless_screen_route::ScreenPreflightMetrics;
    using ScreenBuildFailReason = lossless_screen_route::ScreenBuildFailReason;

    static ScreenPreflightMetrics analyze_screen_indexed_preflight(
        const int16_t* plane, uint32_t width, uint32_t height
    ) {
        return lossless_screen_route::analyze_screen_indexed_preflight(plane, width, height);
    }

    // Thresholds for natural-like texture detection
    struct NaturalThresholds {
        static constexpr uint16_t UNIQUE_MIN = 64;
        static constexpr uint16_t AVG_RUN_MAX_X100 = 460;
        static constexpr uint16_t MAD_MIN_X100 = 20;
        static constexpr uint16_t ENTROPY_MIN_X100 = 5;
        static constexpr uint16_t CHROMA_ROUTE_MAD_MAX_X100 = 80;
        static constexpr uint16_t CHROMA_ROUTE_AVG_RUN_MIN_X100 = 320;
    };

    struct NaturalThresholdRuntime {
        uint16_t unique_min;
        uint16_t avg_run_max_x100;
        uint16_t mad_min_x100;
        uint16_t entropy_min_x100;
    };

    static uint16_t parse_natural_threshold_env(
        const char* key, uint16_t fallback, uint16_t min_v, uint16_t max_v
    ) {
        const char* raw = std::getenv(key);
        if (!raw || raw[0] == '\0') return fallback;
        char* end = nullptr;
        errno = 0;
        long v = std::strtol(raw, &end, 10);
        if (errno != 0 || end == raw || *end != '\0') return fallback;
        if (v < (long)min_v || v > (long)max_v) return fallback;
        return (uint16_t)v;
    }

    static bool parse_bool_env(const char* key, bool fallback) {
        const char* raw = std::getenv(key);
        if (!raw || raw[0] == '\0') return fallback;
        if (std::strcmp(raw, "1") == 0 || std::strcmp(raw, "true") == 0 ||
            std::strcmp(raw, "TRUE") == 0 || std::strcmp(raw, "on") == 0 ||
            std::strcmp(raw, "ON") == 0) {
            return true;
        }
        if (std::strcmp(raw, "0") == 0 || std::strcmp(raw, "false") == 0 ||
            std::strcmp(raw, "FALSE") == 0 || std::strcmp(raw, "off") == 0 ||
            std::strcmp(raw, "OFF") == 0) {
            return false;
        }
        return fallback;
    }

    static bool route_compete_chroma_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_ROUTE_COMPETE_CHROMA", true);
        return kEnabled;
    }

    static bool route_compete_photo_chroma_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_ROUTE_COMPETE_PHOTO_CHROMA", false);
        return kEnabled;
    }

    static bool route_fast_compete_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_FAST_ROUTE_COMPETE", false);
        return kEnabled;
    }

    static bool route_fast_compete_chroma_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_FAST_ROUTE_COMPETE_CHROMA", false);
        return kEnabled;
    }

    static bool route_fast_compete_chroma_conservative() {
        static const bool kEnabled =
            parse_bool_env("HKN_FAST_ROUTE_COMPETE_CHROMA_CONSERVATIVE", true);
        return kEnabled;
    }

    static uint16_t route_fast_lz_nice_length() {
        static const uint16_t kV = parse_natural_threshold_env(
            "HKN_FAST_LZ_NICE_LENGTH", 64, 4, 255
        );
        return kV;
    }

    static uint16_t route_fast_lz_match_strategy() {
        static const uint16_t kV = parse_natural_threshold_env(
            "HKN_FAST_LZ_MATCH_STRATEGY", 0, 0, 1
        );
        return kV;
    }

    static uint16_t route_max_lz_match_strategy() {
        static const uint16_t kV = parse_natural_threshold_env(
            "HKN_MAX_LZ_MATCH_STRATEGY", 1, 0, 2
        );
        return kV;
    }

    static bool route_filter_lo_lz_probe_fast_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_FAST_FILTER_LO_LZ_PROBE", true);
        return kEnabled;
    }

    static bool route_filter_lo_lz_probe_balanced_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_BALANCED_FILTER_LO_LZ_PROBE", false);
        return kEnabled;
    }

    static bool route_filter_lo_lz_probe_max_enabled() {
        static const bool kEnabled = parse_bool_env("HKN_MAX_FILTER_LO_LZ_PROBE", true);
        return kEnabled;
    }

    static LosslessPresetPlan build_lossless_preset_plan(
        LosslessPreset preset, LosslessProfile profile
    ) {
        // DOC: docs/LOSSLESS_FLOW_MAP.md#preset-policy
        LosslessPresetPlan plan{};
        switch (preset) {
            case LosslessPreset::FAST:
                plan.route_compete_luma = route_fast_compete_enabled();
                plan.route_compete_chroma =
                    plan.route_compete_luma && route_fast_compete_chroma_enabled();
                plan.conservative_chroma_route_policy =
                    plan.route_compete_chroma && route_fast_compete_chroma_conservative();
                plan.natural_route_mode2_nice_length_override =
                    plan.route_compete_luma ? (int)route_fast_lz_nice_length() : -1;
                plan.natural_route_mode2_match_strategy_override =
                    plan.route_compete_luma ? (int)route_fast_lz_match_strategy() : -1;
                plan.filter_row_cost_model = lossless_filter_rows::FilterRowCostModel::SAD;
                plan.filter_lo_lz_probe_enable = route_filter_lo_lz_probe_fast_enabled();
                break;
            case LosslessPreset::BALANCED:
                plan.route_compete_luma = true;
                plan.route_compete_chroma = route_compete_chroma_enabled();
                if (profile == LosslessProfile::PHOTO && !route_compete_photo_chroma_enabled()) {
                    plan.route_compete_chroma = false;
                }
                plan.conservative_chroma_route_policy =
                    parse_bool_env("HKN_ROUTE_COMPETE_CHROMA_CONSERVATIVE", false);
                plan.natural_route_mode2_nice_length_override = -1;
                plan.natural_route_mode2_match_strategy_override = -1;
                plan.filter_row_cost_model = lossless_filter_rows::FilterRowCostModel::SAD;
                plan.filter_lo_lz_probe_enable = route_filter_lo_lz_probe_balanced_enabled();
                break;
            case LosslessPreset::MAX:
                // Max mode favors compression: always evaluate route competition on all planes.
                plan.route_compete_luma = true;
                plan.route_compete_chroma = true;
                plan.conservative_chroma_route_policy = false;
                plan.natural_route_mode2_nice_length_override = -1;
                plan.natural_route_mode2_match_strategy_override =
                    (int)route_max_lz_match_strategy();
                plan.filter_row_cost_model = lossless_filter_rows::FilterRowCostModel::ENTROPY;
                plan.filter_lo_lz_probe_enable = route_filter_lo_lz_probe_max_enabled();
                break;
        }
        return plan;
    }

    static uint16_t route_chroma_mad_max_x100() {
        static const uint16_t kV = parse_natural_threshold_env(
            "HKN_ROUTE_CHROMA_MAD_MAX",
            NaturalThresholds::CHROMA_ROUTE_MAD_MAX_X100,
            0, 65535
        );
        return kV;
    }

    static uint16_t route_chroma_avg_run_min_x100() {
        static const uint16_t kV = parse_natural_threshold_env(
            "HKN_ROUTE_CHROMA_AVG_RUN_MIN",
            NaturalThresholds::CHROMA_ROUTE_AVG_RUN_MIN_X100,
            0, 65535
        );
        return kV;
    }

    static const NaturalThresholdRuntime& natural_thresholds_runtime() {
        static const NaturalThresholdRuntime kThresholds = []() {
            NaturalThresholdRuntime t{};
            t.unique_min = parse_natural_threshold_env(
                "HKN_NATURAL_UNIQUE_MIN", NaturalThresholds::UNIQUE_MIN, 0, 65535
            );
            t.avg_run_max_x100 = parse_natural_threshold_env(
                "HKN_NATURAL_AVG_RUN_MAX", NaturalThresholds::AVG_RUN_MAX_X100, 0, 65535
            );
            t.mad_min_x100 = parse_natural_threshold_env(
                "HKN_NATURAL_MAD_MIN", NaturalThresholds::MAD_MIN_X100, 0, 65535
            );
            t.entropy_min_x100 = parse_natural_threshold_env(
                "HKN_NATURAL_ENTROPY_MIN", NaturalThresholds::ENTROPY_MIN_X100, 0, 65535
            );
            return t;
        }();
        return kThresholds;
    }

    static bool is_natural_like(const ScreenPreflightMetrics& m) {
        const auto& t = natural_thresholds_runtime();
        // Natural-like textures: rich value diversity, short runs, and non-trivial edges.
        return !m.likely_screen &&
               (m.unique_sample >= t.unique_min) &&
               (m.avg_run_x100 <= t.avg_run_max_x100) &&
               (m.mean_abs_diff_x100 >= t.mad_min_x100) &&
               (m.run_entropy_hint_x100 >= t.entropy_min_x100);
    }

    static std::vector<uint8_t> encode_plane_lossless_screen_indexed_tile(
        const int16_t* plane, uint32_t width, uint32_t height,
        ScreenBuildFailReason* fail_reason = nullptr
    ) {
        return lossless_screen_route::encode_plane_lossless_screen_indexed_tile(
            plane, width, height, fail_reason,
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            }
        );
    }

    static std::vector<uint8_t> encode_plane_lossless_screen_indexed_tile_padded(
        const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
        ScreenBuildFailReason* fail_reason = nullptr
    ) {
        return lossless_screen_route::encode_plane_lossless_screen_indexed_tile_padded(
            padded, pad_w, pad_h, fail_reason,
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            }
        );
    }

    static std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
        const int16_t* plane, uint32_t width, uint32_t height,
        LosslessModeDebugStats* stats = nullptr,
        int mode2_nice_length_override = -1,
        int mode2_match_strategy_override = -1
    ) {
        return lossless_natural_route::encode_plane_lossless_natural_row_tile(
            plane, width, height,
            [](int16_t v) { return zigzag_encode_val(v); },
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream_shared_lz(bytes);
            },
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            },
            stats,
            mode2_nice_length_override,
            mode2_match_strategy_override
        );
    }

    static std::vector<uint8_t> encode_plane_lossless_natural_row_tile_padded(
        const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
        LosslessModeDebugStats* stats = nullptr,
        int mode2_nice_length_override = -1,
        int mode2_match_strategy_override = -1
    ) {
        return lossless_natural_route::encode_plane_lossless_natural_row_tile_padded(
            padded, pad_w, pad_h,
            [](int16_t v) { return zigzag_encode_val(v); },
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream_shared_lz(bytes);
            },
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            },
            stats,
            mode2_nice_length_override,
            mode2_match_strategy_override
        );
    }

    /**
     * Encode a single int16_t plane losslessly with Screen Profile support.
     * 
     * Hybrid block-based pipeline:
     *   1. Classify each 8x8 block: Palette -> Copy -> Filter
     *   2. Custom row-level filtering (full image context, Palette/Copy as anchors)
     *   3. Filter block residuals -> zigzag -> split lo/hi -> rANS (data-adaptive CDF)
     *
     * Tile format v2 (32-byte header):
     *   [4B filter_ids_size][4B lo_stream_size][4B hi_stream_size][4B filter_pixel_count]
     *   [4B block_types_size][4B palette_data_size][4B copy_data_size][4B reserved]
     *   [filter_ids][lo_stream][hi_stream][block_types][palette_data][copy_data]
     */
    // Backward compatibility wrapper
    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height, bool use_photo_mode_bias
    ) {
        return encode_plane_lossless(data, width, height,
            use_photo_mode_bias ? LosslessProfile::PHOTO : LosslessProfile::UI,
            true,
            false,
            -1,
            -1,
            lossless_filter_rows::FilterRowCostModel::SAD,
            false);
    }

    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height,
        LosslessProfile profile = LosslessProfile::UI,
        bool enable_route_competition = true,
        bool conservative_chroma_route_policy = false,
        int natural_route_mode2_nice_length_override = -1,
        int natural_route_mode2_match_strategy_override = -1,
        lossless_filter_rows::FilterRowCostModel filter_row_cost_model =
            lossless_filter_rows::FilterRowCostModel::SAD,
        bool filter_lo_lz_probe_enable = false
    ) {
        using Clock = std::chrono::steady_clock;
        const auto t_plane_total0 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_calls++;

        // Pad dimensions to multiple of 8
        uint32_t pad_w = ((width + 7) / 8) * 8;
        uint32_t pad_h = ((height + 7) / 8) * 8;
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;

        // Phase 9s-5: Telemetry
        if (profile == LosslessProfile::UI) tl_lossless_mode_debug_stats_.profile_ui_tiles++;
        else if (profile == LosslessProfile::ANIME) tl_lossless_mode_debug_stats_.profile_anime_tiles++;
        else tl_lossless_mode_debug_stats_.profile_photo_tiles++;

        // Pad the int16_t image
        const auto t_pad0 = Clock::now();
        std::vector<int16_t> padded(pad_w * pad_h, 0);
        for (uint32_t y = 0; y < pad_h; y++) {
            for (uint32_t x = 0; x < pad_w; x++) {
                padded[y * pad_w + x] = data[std::min(y, height - 1) * width + std::min(x, width - 1)];
            }
        }
        const auto t_pad1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_pad_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pad1 - t_pad0).count();
        // --- Step 1: Block classification ---
        const auto t_cls0 = Clock::now();
        auto cls = lossless_block_classifier::classify_blocks(
            padded,
            pad_w,
            pad_h,
            static_cast<int>(profile),
            &tl_lossless_mode_debug_stats_
        );
        const auto t_cls1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_block_classify_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_cls1 - t_cls0).count();
        std::vector<FileHeader::BlockType> block_types = std::move(cls.block_types);
        std::vector<Palette> palettes = std::move(cls.palettes);
        std::vector<std::vector<uint8_t>> palette_indices = std::move(cls.palette_indices);
        std::vector<CopyParams> copy_ops = std::move(cls.copy_ops);
        std::vector<lossless_tile4_codec::Tile4Result> tile4_results = std::move(cls.tile4_results);

        // --- Step 2: Custom filtering (block-type aware, full image context) ---
        const auto t_filter_rows0 = Clock::now();
        std::vector<uint8_t> filter_ids;
        std::vector<int16_t> filter_residuals;
        lossless_filter_rows::build_filter_rows_and_residuals(
            padded,
            pad_w,
            pad_h,
            nx,
            block_types,
            static_cast<int>(profile),
            &tl_lossless_mode_debug_stats_,
            filter_ids,
            filter_residuals,
            filter_row_cost_model
        );
        const auto t_filter_rows1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_filter_rows_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_filter_rows1 - t_filter_rows0).count();

        // --- Step 3: ZigZag + rANS encode filter residuals (data-adaptive CDF) ---
        std::vector<uint8_t> lo_stream, hi_stream;
        uint32_t filter_pixel_count = (uint32_t)filter_residuals.size();

        if (!filter_residuals.empty()) {
            std::vector<uint8_t> lo_bytes(filter_pixel_count), hi_bytes(filter_pixel_count);
            for (size_t i = 0; i < filter_pixel_count; i++) {
                uint16_t zz = zigzag_encode_val(filter_residuals[i]);
                lo_bytes[i] = (uint8_t)(zz & 0xFF);
                hi_bytes[i] = (uint8_t)((zz >> 8) & 0xFF);
            }
            const auto t_lo0 = Clock::now();
            lo_stream = lossless_filter_lo_codec::encode_filter_lo_stream(
                lo_bytes,
                filter_ids,
                block_types,
                pad_h,
                nx,
                static_cast<int>(profile),
                &tl_lossless_mode_debug_stats_,
                [](const std::vector<uint8_t>& bytes) {
                    return GrayscaleEncoder::encode_byte_stream(bytes);
                },
                [](const std::vector<uint8_t>& bytes) {
                    return GrayscaleEncoder::encode_byte_stream_shared_lz(bytes);
                },
                [](const std::vector<uint8_t>& bytes) {
                    return TileLZ::compress(bytes);
                },
                filter_lo_lz_probe_enable
            );
            const auto t_lo1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_lo_stream_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_lo1 - t_lo0).count();

            // --- Phase 9n: filter_hi sparse-or-dense wrapper ---
            const auto t_hi0 = Clock::now();
            hi_stream = filter_hi_wrapper::encode_filter_hi_stream(
                hi_bytes, &tl_lossless_mode_debug_stats_
            );
            const auto t_hi1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_hi_stream_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_hi1 - t_hi0).count();
        }

        // --- Step 4: Encode block types, palette, copy, tile4 ---
        // --- Step 4: Encode block types, palette, copy, tile4 ---
        const auto t_wrap0 = Clock::now();
        std::vector<uint8_t> bt_data = encode_block_types(block_types, true);
        
        int reorder_trials = 0;
        int reorder_adopted = 0;
        std::vector<uint8_t> pal_raw = PaletteCodec::encode_palette_stream(
            palettes, palette_indices, true, &reorder_trials, &reorder_adopted
        );
        tl_lossless_mode_debug_stats_.palette_reorder_trials += reorder_trials;
        tl_lossless_mode_debug_stats_.palette_reorder_adopted += reorder_adopted;
        accumulate_palette_stream_diagnostics(pal_raw, tl_lossless_mode_debug_stats_);
        std::vector<uint8_t> pal_data = lossless_stream_wrappers::wrap_palette_stream(
            pal_raw,
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            },
            [](const std::vector<uint8_t>& bytes) {
                return TileLZ::compress(bytes);
            },
            &tl_lossless_mode_debug_stats_
        );

        auto copy_wrap = lossless_stream_wrappers::wrap_copy_stream(
            copy_ops,
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            },
            [](const std::vector<uint8_t>& bytes) {
                return TileLZ::compress(bytes);
            },
            &tl_lossless_mode_debug_stats_
        );
        std::vector<uint8_t> cpy_raw = std::move(copy_wrap.raw);
        std::vector<uint8_t> cpy_data = std::move(copy_wrap.wrapped);
        int copy_wrapper_mode = copy_wrap.mode;
        // Encode Tile4 stream using module function
        std::vector<uint8_t> tile4_data = lossless_tile4_codec::encode_tile4_stream(
            tile4_results,
            [](const std::vector<uint8_t>& bytes) -> std::vector<uint8_t> {
                return byte_stream_encoder::encode_byte_stream(bytes);
            }
        );
        int tile4_mode = lossless_stream_wrappers::detect_wrapper_mode(
            tile4_data, FileHeader::WRAPPER_MAGIC_TILE4
        );
        // Compute raw tile4 size for diagnostics (reverse of serialization)
        size_t tile4_raw_size = tile4_results.size() * 2;

        // Stream-level diagnostics for lossless mode decision tuning.
        lossless_stream_diagnostics::accumulate(
            tl_lossless_mode_debug_stats_,
            bt_data,
            pal_data,
            tile4_data,
            tile4_raw_size,
            copy_ops,
            cpy_raw,
            cpy_data,
            copy_wrapper_mode
        );
        const auto t_wrap1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_stream_wrap_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_wrap1 - t_wrap0).count();

        // --- Step 5: Compress filter_ids (Phase 9n) ---
        const auto t_fid0 = Clock::now();
        std::vector<uint8_t> filter_ids_packed = lossless_stream_wrappers::wrap_filter_ids_stream(
            filter_ids,
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            },
            [](const std::vector<uint8_t>& bytes) {
                return TileLZ::compress(bytes);
            },
            &tl_lossless_mode_debug_stats_
        );
        const auto t_fid1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_filter_ids_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_fid1 - t_fid0).count();

        // --- Step 6: Pack tile data (32-byte header) ---
        const auto t_pack0 = Clock::now();
        std::vector<uint8_t> tile_data = lossless_tile_packer::pack_tile_v2(
            filter_ids_packed,
            lo_stream,
            hi_stream,
            filter_pixel_count,
            bt_data,
            pal_data,
            cpy_data,
            tile4_data
        );
        const auto t_pack1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_pack_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_pack0).count();
        if (!enable_route_competition) {
            tl_lossless_mode_debug_stats_.route_compete_policy_skip_count++;
            tl_lossless_mode_debug_stats_.perf_encode_plane_total_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_plane_total0).count();
            return tile_data;
        }
        bool route_prefilter_cached = false;
        ScreenPreflightMetrics route_prefilter_cache{};
        if (conservative_chroma_route_policy) {
            const auto m = analyze_screen_indexed_preflight(data, width, height);
            route_prefilter_cached = true;
            route_prefilter_cache = m;
            const bool allow_chroma_route =
                (m.mean_abs_diff_x100 <= route_chroma_mad_max_x100()) &&
                (m.avg_run_x100 >= route_chroma_avg_run_min_x100());
            if (!allow_chroma_route) {
                tl_lossless_mode_debug_stats_.route_compete_policy_skip_count++;
                tl_lossless_mode_debug_stats_.perf_encode_plane_total_ns +=
                    (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_pack1 - t_plane_total0).count();
                return tile_data;
            }
        }

        const auto t_route0 = Clock::now();
        auto best_tile = lossless_route_competition::choose_best_tile(
            tile_data,
            data,
            width,
            height,
            static_cast<int>(profile),
            &tl_lossless_mode_debug_stats_,
            [route_prefilter_cached, route_prefilter_cache](const int16_t* p, uint32_t w, uint32_t h) {
                if (route_prefilter_cached) return route_prefilter_cache;
                return GrayscaleEncoder::analyze_screen_indexed_preflight(p, w, h);
            },
            [&padded, pad_w, pad_h](const int16_t*, uint32_t, uint32_t, ScreenBuildFailReason* fail_reason) {
                return GrayscaleEncoder::encode_plane_lossless_screen_indexed_tile_padded(
                    padded.data(), pad_w, pad_h, fail_reason
                );
            },
            [](const ScreenPreflightMetrics& m) {
                return GrayscaleEncoder::is_natural_like(m);
            },
            [&padded, pad_w, pad_h, natural_route_mode2_nice_length_override,
             natural_route_mode2_match_strategy_override](
                const int16_t*, uint32_t, uint32_t
            ) {
                return GrayscaleEncoder::encode_plane_lossless_natural_row_tile_padded(
                    padded.data(),
                    pad_w,
                    pad_h,
                    &tl_lossless_mode_debug_stats_,
                    natural_route_mode2_nice_length_override,
                    natural_route_mode2_match_strategy_override
                );
            }
        );
        const auto t_route1 = Clock::now();
        tl_lossless_mode_debug_stats_.perf_encode_plane_route_compete_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_route1 - t_route0).count();
        tl_lossless_mode_debug_stats_.perf_encode_plane_total_ns +=
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_route1 - t_plane_total0).count();
        return best_tile;
    }
