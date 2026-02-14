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
