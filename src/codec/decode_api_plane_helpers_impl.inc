public:
    struct BandPIndexBundle {
        bool has_low = false;
        bool has_mid = false;
        bool has_high = false;
        PIndex low;
        PIndex mid;
        PIndex high;
    };

    static bool parse_band_pindex_blob(
        const uint8_t* data, size_t size,
        size_t low_stream_size, size_t mid_stream_size, size_t high_stream_size,
        BandPIndexBundle& out
    ) {
        if (size == 0) return false;
        if (size < 12) return false;

        uint32_t low_sz = 0, mid_sz = 0, high_sz = 0;
        std::memcpy(&low_sz, data, 4);
        std::memcpy(&mid_sz, data + 4, 4);
        std::memcpy(&high_sz, data + 8, 4);
        size_t expected = 12ull + (size_t)low_sz + (size_t)mid_sz + (size_t)high_sz;
        if (expected != size) return false;

        const uint8_t* ptr = data + 12;
        try {
            if (low_sz > 0) {
                out.low = PIndexCodec::deserialize(std::span<const uint8_t>(ptr, low_sz));
                out.has_low = (out.low.total_bytes == low_stream_size && out.low.total_tokens > 0);
            }
            ptr += low_sz;
            if (mid_sz > 0) {
                out.mid = PIndexCodec::deserialize(std::span<const uint8_t>(ptr, mid_sz));
                out.has_mid = (out.mid.total_bytes == mid_stream_size && out.mid.total_tokens > 0);
            }
            ptr += mid_sz;
            if (high_sz > 0) {
                out.high = PIndexCodec::deserialize(std::span<const uint8_t>(ptr, high_sz));
                out.has_high = (out.high.total_bytes == high_stream_size && out.high.total_tokens > 0);
            }
        } catch (...) {
            return false;
        }

        return out.has_low || out.has_mid || out.has_high;
    }

    static void parse_cfl_stream(
        const uint8_t* cfl_ptr,
        uint32_t sz_cfl,
        int nb,
        std::vector<CfLParams>& cfls,
        bool& centered_predictor
    ) {
        centered_predictor = false;
        cfls.clear();
        if (!cfl_ptr || sz_cfl == 0 || nb <= 0) return;

        const size_t nb_sz = (size_t)nb;
        const size_t legacy_size = nb_sz * 2;
        const size_t mask_bytes = (nb_sz + 7) / 8;

        auto parse_legacy = [&]() {
            size_t pairs = std::min(nb_sz, (size_t)sz_cfl / 2);
            cfls.reserve(nb_sz);
            for (size_t i = 0; i < pairs; i++) {
                float a = (int8_t)cfl_ptr[i * 2] / 64.0f;
                float b = cfl_ptr[i * 2 + 1];
                // Legacy stream applies predictor for every block.
                cfls.push_back({a, b, 1.0f, 0.0f});
            }
            if (pairs < nb_sz) {
                cfls.resize(nb_sz, {0.0f, 128.0f, 0.0f, 0.0f});
            }
            centered_predictor = false;
        };

        auto try_parse_adaptive = [&]() -> bool {
            if ((size_t)sz_cfl < mask_bytes) return false;
            size_t applied = 0;
            for (int i = 0; i < nb; i++) {
                if (cfl_ptr[(size_t)i / 8] & (uint8_t)(1u << (i % 8))) applied++;
            }
            size_t expected = mask_bytes + applied * 2;
            if (expected != (size_t)sz_cfl) return false;

            cfls.assign(nb_sz, {0.0f, 128.0f, 0.0f, 0.0f});
            const uint8_t* param_ptr = cfl_ptr + mask_bytes;
            for (int i = 0; i < nb; i++) {
                if (cfl_ptr[(size_t)i / 8] & (uint8_t)(1u << (i % 8))) {
                    float a = (int8_t)(*param_ptr++) / 64.0f;
                    float b = *param_ptr++;
                    cfls[(size_t)i] = {a, b, 1.0f, 0.0f};
                }
            }
            centered_predictor = true;
            return true;
        };

        // Prefer legacy when byte size exactly matches historical stream.
        if ((size_t)sz_cfl == legacy_size) {
            parse_legacy();
            return;
        }
        if (try_parse_adaptive()) return;
        if ((size_t)sz_cfl % 2 == 0) {
            parse_legacy();
            return;
        }
        // Malformed/unknown: disable CfL for this tile.
        cfls.assign(nb_sz, {0.0f, 128.0f, 0.0f, 0.0f});
        centered_predictor = false;
    }
