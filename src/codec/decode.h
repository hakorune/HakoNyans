#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/parallel_decode.h"
#include "../simd/simd_dispatch.h"
#include "../platform/thread_budget.h"
#include "../platform/thread_pool.h"
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <future>
#include <cmath>
#include <iostream>
#include "palette.h"
#include "copy.h"
#include "lossless_filter.h"
#include "band_groups.h"
#include "lz_tile.h"
#include "shared_cdf.h"
#include "lossless_block_types_codec.h"
#include "lossless_decode_debug_stats.h"
#include "lossless_plane_decode_core.h"

namespace hakonyans {

class GrayscaleDecoder {
public:
    using LosslessDecodeDebugStats = ::hakonyans::LosslessDecodeDebugStats;

    static void reset_lossless_decode_debug_stats() {
        tl_lossless_decode_debug_stats_.reset();
    }

    static LosslessDecodeDebugStats get_lossless_decode_debug_stats() {
        return tl_lossless_decode_debug_stats_;
    }

private:
    inline static thread_local LosslessDecodeDebugStats tl_lossless_decode_debug_stats_;

    static bool env_bool_flag(const char* key, bool fallback) {
        const char* raw = std::getenv(key);
        if (!raw || raw[0] == '\0') return fallback;
        const char c0 = raw[0];
        if (c0 == '0' || c0 == 'f' || c0 == 'F' || c0 == 'n' || c0 == 'N') return false;
        if (c0 == '1' || c0 == 't' || c0 == 'T' || c0 == 'y' || c0 == 'Y') return true;
        return fallback;
    }

    static bool decode_use_bulk_rans() {
        static const bool enabled = env_bool_flag("HKN_DECODE_BULK_RANS", true);
        return enabled;
    }

    static bool decode_use_plane_caller_y_path() {
        static const bool enabled = env_bool_flag("HKN_DECODE_PLANE_CALLER_Y", false);
        return enabled;
    }

    static ThreadPool& decode_worker_pool() {
        static ThreadPool pool((int)std::max(1u, thread_budget::max_threads(8)));
        return pool;
    }

    static bool try_build_cdf_from_serialized_freq(
        const uint8_t* cdf_data,
        uint32_t cdf_size,
        CDFTable& out
    ) {
        if (!cdf_data || cdf_size == 0 || (cdf_size & 3u) != 0u) return false;
        const int alpha = (int)(cdf_size / 4u);
        if (alpha <= 0 || alpha > 256) return false;

        thread_local std::vector<uint32_t> tl_freq;
        thread_local std::vector<uint32_t> tl_cdf;
        if ((int)tl_freq.size() < alpha) tl_freq.resize((size_t)alpha);
        if ((int)tl_cdf.size() < alpha + 1) tl_cdf.resize((size_t)alpha + 1);

        std::memcpy(tl_freq.data(), cdf_data, cdf_size);
        uint32_t sum = 0;
        tl_cdf[0] = 0;
        for (int i = 0; i < alpha; i++) {
            const uint32_t f = tl_freq[(size_t)i];
            if (f == 0) return false;
            sum += f;
            tl_cdf[(size_t)i + 1] = sum;
        }
        if (sum != RANS_TOTAL) return false;

        out.total = RANS_TOTAL;
        out.cdf = tl_cdf.data();
        out.freq = tl_freq.data();
        out.alphabet_size = alpha;
        return true;
    }

    static void build_simd_table_inplace(const CDFTable& cdf, SIMDDecodeTable& table) {
        table.alphabet_size = cdf.alphabet_size;
        std::memset(table.freq, 0, sizeof(table.freq));
        std::memset(table.bias, 0, sizeof(table.bias));

        for (int i = 0; i < cdf.alphabet_size; i++) {
            table.freq[i] = cdf.freq[i];
            table.bias[i] = cdf.cdf[i];
        }
        for (int sym = 0; sym < cdf.alphabet_size; sym++) {
            const uint32_t lo = cdf.cdf[sym];
            const uint32_t hi = cdf.cdf[sym + 1];
            for (uint32_t slot = lo; slot < hi; slot++) {
                table.slot_to_symbol[slot] = (uint32_t)sym;
            }
        }
    }

public:
    #include "decode_api_impl.h"

private:
    static const CDFTable& get_mode5_shared_lz_cdf() {
        static const CDFTable cdf = CDFBuilder().build_from_freq(mode5_shared_lz_freq());
        return cdf;
    }

    static const SIMDDecodeTable& get_mode5_shared_lz_simd_table() {
        static const SIMDDecodeTable tbl = []() {
            auto p = CDFBuilder::build_simd_table(get_mode5_shared_lz_cdf());
            return *p;
        }();
        return tbl;
    }
};

} // namespace hakonyans
