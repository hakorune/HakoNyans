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
