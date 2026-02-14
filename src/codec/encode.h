#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/pindex.h"
#include "palette.h"
#include "copy.h"
#include "lossless_filter.h"
#include "band_groups.h"
#include "lz_tile.h"
#include "shared_cdf.h"
#include "lossless_mode_debug_stats.h"
#include "lossless_screen_helpers.h"
#include "lossless_mode_select.h"
#include "lossless_screen_route.h"
#include "lossless_natural_route.h"
#include "lossless_profile_classifier.h"
#include "lossless_palette_diagnostics.h"
#include "lossless_block_types_codec.h"
#include "lossless_filter_lo_codec.h"
#include "lossless_stream_wrappers.h"
#include "lossless_tile_packer.h"
#include "lossless_filter_rows.h"
#include "lossless_block_classifier.h"
#include "lossless_route_competition.h"
#include "lossless_stream_diagnostics.h"
#include "lossy_tile_packer.h"
#include "lossy_plane_analysis.h"
// New modular components
#include "cfl_codec.h"
#include "token_stream_codec.h"
#include "lossy_image_helpers.h"
#include "lossless_tile4_codec.h"
#include "byte_stream_encoder.h"
#include "filter_hi_wrapper.h"
#include "../platform/thread_budget.h"
#include <vector>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace hakonyans {

/**
 * Lossless tile data layout:
 *   [4 bytes] filter_ids_size
 *   [4 bytes] lo_stream_size    (rANS-encoded low bytes)
 *   [4 bytes] hi_stream_size    (rANS-encoded high bytes)
 *   [4 bytes] pindex_size
 *   [filter_ids_size bytes] filter IDs (1 per row, per plane)
 *   [lo_stream_size bytes]  rANS stream for low bytes
 *   [hi_stream_size bytes]  rANS stream for high bytes
 *   [pindex_size bytes]     P-Index data (optional)
 */

class GrayscaleEncoder {
public:
    using LosslessModeDebugStats = ::hakonyans::LosslessModeDebugStats;

    static void reset_lossless_mode_debug_stats() {
        tl_lossless_mode_debug_stats_.reset();
    }

    static LosslessModeDebugStats get_lossless_mode_debug_stats() {
        return tl_lossless_mode_debug_stats_;
    }

private:
    inline static thread_local LosslessModeDebugStats tl_lossless_mode_debug_stats_;

public:
    enum class LosslessProfile : uint8_t { UI = 0, ANIME = 1, PHOTO = 2 };
    enum class LosslessPreset : uint8_t { FAST = 0, BALANCED = 1, MAX = 2 };

    static const char* lossless_preset_name(LosslessPreset preset) {
        switch (preset) {
            case LosslessPreset::FAST: return "fast";
            case LosslessPreset::BALANCED: return "balanced";
            case LosslessPreset::MAX: return "max";
        }
        return "balanced";
    }

private:
    // DOC: docs/LOSSLESS_FLOW_MAP.md#preset-policy
    struct LosslessPresetPlan {
        bool route_compete_luma = true;
        bool route_compete_chroma = true;
        bool conservative_chroma_route_policy = false;
        int natural_route_mode2_nice_length_override = -1;
        int natural_route_mode2_match_strategy_override = -1; // -1=runtime default
        lossless_filter_rows::FilterRowCostModel filter_row_cost_model =
            lossless_filter_rows::FilterRowCostModel::SAD;
        bool filter_lo_lz_probe_enable = false;
    };

public:
    // Heuristic profile for applying lossless mode biases.
    // Classify from sampled exact-copy hit rate, local gradient, and histogram density.
    static LosslessProfile classify_lossless_profile(const int16_t* y_plane, uint32_t width, uint32_t height) {
        auto p = lossless_profile_classifier::classify(
            y_plane, width, height, &tl_lossless_mode_debug_stats_
        );
        if (p == lossless_profile_classifier::Profile::UI) return LosslessProfile::UI;
        if (p == lossless_profile_classifier::Profile::ANIME) return LosslessProfile::ANIME;
        return LosslessProfile::PHOTO;
    }

    // Delegations to cfl_codec module
    static uint32_t extract_tile_cfl_size(const std::vector<uint8_t>& tile_data, bool use_band_group_cdf) {
        return cfl_codec::extract_tile_cfl_size(tile_data, use_band_group_cdf);
    }

    static std::vector<uint8_t> serialize_cfl_legacy(const std::vector<CfLParams>& cfl_params) {
        return cfl_codec::serialize_cfl_legacy(cfl_params);
    }

    static std::vector<uint8_t> serialize_cfl_adaptive(const std::vector<CfLParams>& cfl_params) {
        return cfl_codec::serialize_cfl_adaptive(cfl_params);
    }

    static std::vector<uint8_t> build_cfl_payload(const std::vector<CfLParams>& cfl_params) {
        return cfl_codec::build_cfl_payload(cfl_params);
    }

    static std::vector<uint8_t> encode(const uint8_t* pixels, uint32_t width, uint32_t height, uint8_t quality = 75) {
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 1; header.colorspace = 2; header.subsampling = 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        uint32_t pad_w = header.padded_width(), pad_h = header.padded_height();
        uint16_t quant[64]; QuantTable::build_quant_table(quality, quant);
        int target_pi_meta_ratio = (quality >= 90) ? 1 : 2;
        auto tile_data = encode_plane(
            pixels, width, height, pad_w, pad_h, quant,
            true, true, nullptr, 0, nullptr, nullptr, false, true,
            target_pi_meta_ratio
        );
        QMATChunk qmat; qmat.quality = quality; qmat.num_tables = 1; std::memcpy(qmat.quant_y, quant, 128);
        auto qmat_data = qmat.serialize();
        ChunkDirectory dir; dir.add("QMAT", 0, qmat_data.size()); dir.add("TIL0", 0, tile_data.size());
        auto dir_data = dir.serialize();
        size_t qmat_offset = 48 + dir_data.size();
        size_t tile_offset = qmat_offset + qmat_data.size();
        dir.entries[0].offset = qmat_offset; dir.entries[1].offset = tile_offset;
        dir_data = dir.serialize();
        std::vector<uint8_t> output; output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        return output;
    }

    static std::vector<uint8_t> encode_color(const uint8_t* rgb_data, uint32_t width, uint32_t height, uint8_t quality = 75, bool use_420 = true, bool use_cfl = true, bool enable_screen_profile = false) {
        std::vector<uint8_t> y_plane(width * height), cb_plane(width * height), cr_plane(width * height);
        for (uint32_t i = 0; i < width * height; i++) rgb_to_ycbcr(rgb_data[i*3], rgb_data[i*3+1], rgb_data[i*3+2], y_plane[i], cb_plane[i], cr_plane[i]);
        bool use_band_group_cdf = (quality <= 70);
        int target_pi_meta_ratio = (quality >= 90) ? 1 : 2;
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 3; header.colorspace = 0; header.subsampling = use_420 ? 1 : 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        if (!use_band_group_cdf) header.version = FileHeader::MIN_SUPPORTED_VERSION;  // v0.3 legacy AC stream
        uint16_t quant_y[64], quant_c[64];
        int chroma_quality = std::clamp((int)quality - 12, 1, 100);
        QuantTable::build_quant_tables(quality, chroma_quality, quant_y, quant_c);
        uint32_t pad_w_y = header.padded_width(), pad_h_y = header.padded_height();
        auto tile_y = encode_plane(
            y_plane.data(), width, height, pad_w_y, pad_h_y, quant_y,
            true, true, nullptr, 0, nullptr, nullptr,
            enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
        );
        std::vector<uint8_t> tile_cb, tile_cr;
        bool any_cfl_payload = false;
        auto encode_chroma_best = [&](const uint8_t* chroma_pixels, uint32_t cw, uint32_t ch, uint32_t cpw, uint32_t cph, const std::vector<uint8_t>* y_for_cfl, int cidx) {
            auto without_cfl = encode_plane(
                chroma_pixels, cw, ch, cpw, cph, quant_c,
                true, true, nullptr, cidx, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
            if (!use_cfl || y_for_cfl == nullptr) return without_cfl;

            auto with_cfl = encode_plane(
                chroma_pixels, cw, ch, cpw, cph, quant_c,
                true, true, y_for_cfl, cidx, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
            if (with_cfl.size() < without_cfl.size()) {
                any_cfl_payload |= (extract_tile_cfl_size(with_cfl, use_band_group_cdf) > 0);
                return with_cfl;
            }
            return without_cfl;
        };

        if (use_420) {
            int cb_w, cb_h; std::vector<uint8_t> cb_420, cr_420, y_ds;
            downsample_420(cb_plane.data(), width, height, cb_420, cb_w, cb_h);
            downsample_420(cr_plane.data(), width, height, cr_420, cb_w, cb_h);
            uint32_t pad_w_c = ((cb_w + 7) / 8) * 8, pad_h_c = ((cb_h + 7) / 8) * 8;
            if (use_cfl) { downsample_420(y_plane.data(), width, height, y_ds, cb_w, cb_h); }
            tile_cb = encode_chroma_best(cb_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, use_cfl ? &y_ds : nullptr, 0);
            tile_cr = encode_chroma_best(cr_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, use_cfl ? &y_ds : nullptr, 1);
        } else {
            tile_cb = encode_chroma_best(cb_plane.data(), width, height, pad_w_y, pad_h_y, use_cfl ? &y_plane : nullptr, 0);
            tile_cr = encode_chroma_best(cr_plane.data(), width, height, pad_w_y, pad_h_y, use_cfl ? &y_plane : nullptr, 1);
        }
        if (any_cfl_payload) header.flags |= 2;

        QMATChunk qmat;
        qmat.quality = quality;
        qmat.num_tables = 3;
        std::memcpy(qmat.quant_y, quant_y, 128);
        std::memcpy(qmat.quant_cb, quant_c, 128);
        std::memcpy(qmat.quant_cr, quant_c, 128);
        auto qmat_data = qmat.serialize();
        ChunkDirectory dir; dir.add("QMAT", 0, qmat_data.size()); dir.add("TIL0", 0, tile_y.size()); dir.add("TIL1", 0, tile_cb.size()); dir.add("TIL2", 0, tile_cr.size());
        auto dir_data = dir.serialize(); size_t offset = 48 + dir_data.size();
        for (int i = 0; i < 4; i++) { dir.entries[i].offset = offset; offset += (i==0?qmat_data.size():(i==1?tile_y.size():(i==2?tile_cb.size():tile_cr.size()))); }
        dir_data = dir.serialize();
        std::vector<uint8_t> output; output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end()); output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_y.begin(), tile_y.end()); output.insert(output.end(), tile_cb.begin(), tile_cb.end()); output.insert(output.end(), tile_cr.begin(), tile_cr.end());
        return output;
    }

public:
    static std::vector<uint8_t> encode_plane(
        const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h,
        const uint16_t quant[64], bool pi=false, bool aq=false, const std::vector<uint8_t>* y_ref=nullptr, int chroma_idx=0,
        const std::vector<FileHeader::BlockType>* block_types_in = nullptr,
        const std::vector<CopyParams>* copy_params_in = nullptr,
        bool enable_screen_profile = false,
        bool use_band_group_cdf = true,
        int target_pindex_meta_ratio_percent = 2
    ) {
        auto analysis = lossy_plane_analysis::analyze_blocks_and_tokenize(
            pixels,
            width,
            height,
            pad_w,
            pad_h,
            quant,
            aq,
            y_ref,
            block_types_in,
            copy_params_in,
            enable_screen_profile,
            use_band_group_cdf
        );

        std::vector<uint8_t> bt_data = encode_block_types(analysis.block_types);

        int reorder_trials = 0;
        int reorder_adopted = 0;
        std::vector<uint8_t> pal_data = PaletteCodec::encode_palette_stream(
             analysis.palettes, analysis.palette_indices, false,
             &reorder_trials, &reorder_adopted
        );
        tl_lossless_mode_debug_stats_.palette_reorder_trials += reorder_trials;
        tl_lossless_mode_debug_stats_.palette_reorder_adopted += reorder_adopted;

        if (!pal_data.empty()) {
             std::vector<uint8_t> lz = TileLZ::compress(pal_data);
             size_t wrapped_size = 6 + lz.size();
             if (wrapped_size * 100 <= pal_data.size() * 98) {
                 std::vector<uint8_t> wrapped;
                 wrapped.resize(6);
                 wrapped[0] = FileHeader::WRAPPER_MAGIC_PALETTE;
                 wrapped[1] = 2;
                 uint32_t rc = (uint32_t)pal_data.size();
                 std::memcpy(&wrapped[2], &rc, 4);
                 wrapped.insert(wrapped.end(), lz.begin(), lz.end());

                 tl_lossless_mode_debug_stats_.palette_lz_used_count++;
                 tl_lossless_mode_debug_stats_.palette_lz_saved_bytes_sum += (pal_data.size() - wrapped.size());

                 pal_data = std::move(wrapped);
             }
        }

        std::vector<uint8_t> cpy_data = CopyCodec::encode_copy_stream(analysis.copy_ops);
        if (!cpy_data.empty()) {
            std::vector<uint8_t> lz = TileLZ::compress(cpy_data);
            size_t wrapped_size = 6 + lz.size();
            if (wrapped_size * 100 <= cpy_data.size() * 98) {
                std::vector<uint8_t> wrapped;
                wrapped.resize(6);
                wrapped[0] = FileHeader::WRAPPER_MAGIC_COPY;
                wrapped[1] = 2;
                uint32_t rc = (uint32_t)cpy_data.size();
                std::memcpy(&wrapped[2], &rc, 4);
                wrapped.insert(wrapped.end(), lz.begin(), lz.end());

                tl_lossless_mode_debug_stats_.copy_lz_used_count++;
                tl_lossless_mode_debug_stats_.copy_lz_saved_bytes_sum += (cpy_data.size() - wrapped.size());

                cpy_data = std::move(wrapped);
            }
        }

        std::vector<uint8_t> pindex_data;
        std::vector<uint8_t> cfl_data = build_cfl_payload(analysis.cfl_params);

        auto dc_cdf = build_cdf(analysis.dc_tokens);
        auto dc_stream = encode_tokens(analysis.dc_tokens, dc_cdf);
        CDFBuilder::cleanup(dc_cdf);
        std::vector<uint8_t> tile_data;
        if (use_band_group_cdf) {
            constexpr size_t kBandPindexMinStreamBytes = 32 * 1024;
            std::vector<uint8_t> pindex_low, pindex_mid, pindex_high;
            auto ac_low_cdf = build_cdf(analysis.ac_low_tokens);
            auto ac_low_stream = encode_tokens(
                analysis.ac_low_tokens, ac_low_cdf, pi ? &pindex_low : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            CDFBuilder::cleanup(ac_low_cdf);
            auto ac_mid_cdf = build_cdf(analysis.ac_mid_tokens);
            auto ac_mid_stream = encode_tokens(
                analysis.ac_mid_tokens, ac_mid_cdf, pi ? &pindex_mid : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            CDFBuilder::cleanup(ac_mid_cdf);
            auto ac_high_cdf = build_cdf(analysis.ac_high_tokens);
            auto ac_high_stream = encode_tokens(
                analysis.ac_high_tokens, ac_high_cdf, pi ? &pindex_high : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            CDFBuilder::cleanup(ac_high_cdf);
            pindex_data = serialize_band_pindex_blob(pindex_low, pindex_mid, pindex_high);
            tile_data = lossy_tile_packer::pack_band_group_tile(
                dc_stream,
                ac_low_stream,
                ac_mid_stream,
                ac_high_stream,
                pindex_data,
                analysis.q_deltas,
                cfl_data,
                bt_data,
                pal_data,
                cpy_data
            );
        } else {
            auto ac_cdf = build_cdf(analysis.ac_tokens);
            auto ac_stream = encode_tokens(
                analysis.ac_tokens, ac_cdf, pi ? &pindex_data : nullptr,
                target_pindex_meta_ratio_percent
            );
            CDFBuilder::cleanup(ac_cdf);
            tile_data = lossy_tile_packer::pack_legacy_tile(
                dc_stream,
                ac_stream,
                pindex_data,
                analysis.q_deltas,
                cfl_data,
                bt_data,
                pal_data,
                cpy_data
            );
        }

        (void)chroma_idx;
        return tile_data;
    }

    // Delegations to token_stream_codec module
    static CDFTable build_cdf(const std::vector<Token>& t) {
        return token_stream_codec::build_cdf(t);
    }

    static int calculate_pindex_interval(
        size_t token_count,
        size_t encoded_token_stream_bytes,
        int target_meta_ratio_percent = 2
    ) {
        return token_stream_codec::calculate_pindex_interval(
            token_count, encoded_token_stream_bytes, target_meta_ratio_percent
        );
    }

    static std::vector<uint8_t> serialize_band_pindex_blob(
        const std::vector<uint8_t>& low,
        const std::vector<uint8_t>& mid,
        const std::vector<uint8_t>& high
    ) {
        return token_stream_codec::serialize_band_pindex_blob(low, mid, high);
    }

    static std::vector<uint8_t> encode_tokens(
        const std::vector<Token>& t,
        const CDFTable& c,
        std::vector<uint8_t>* out_pi = nullptr,
        int target_pindex_meta_ratio_percent = 2,
        size_t min_pindex_stream_bytes = 0
    ) {
        return token_stream_codec::encode_tokens(
            t, c, out_pi, target_pindex_meta_ratio_percent, min_pindex_stream_bytes
        );
    }

    // Delegations to lossy_image_helpers module
    static std::vector<uint8_t> pad_image(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h) {
        return lossy_image_helpers::pad_image(pixels, width, height, pad_w, pad_h);
    }

    static void extract_block(const uint8_t* pixels, uint32_t stride, uint32_t height, int bx, int by, int16_t block[64]) {
        lossy_image_helpers::extract_block(pixels, stride, height, bx, by, block);
    }

public:
    static std::vector<uint8_t> encode_block_types(
        const std::vector<FileHeader::BlockType>& types,
        bool allow_compact = false
    ) {
        return lossless_block_types_codec::encode_block_types(
            types,
            allow_compact,
            [](const std::vector<uint8_t>& raw) {
                return GrayscaleEncoder::encode_byte_stream(raw);
            },
            [](const std::vector<uint8_t>& raw) {
                return TileLZ::compress(raw);
            },
            &tl_lossless_mode_debug_stats_
        );
    }

    static void accumulate_palette_stream_diagnostics(
        const std::vector<uint8_t>& pal_raw,
        LosslessModeDebugStats& s
    ) {
        lossless_palette_diagnostics::accumulate(pal_raw, s);
    }

    // ========================================================================
    // Lossless encoding
    // ========================================================================

    /**
     * Encode a grayscale image losslessly.
     */
    #include "encode_lossless_impl.h"



    /**
     * Encode a byte stream using rANS with data-adaptive CDF.
     * Format: [4B cdf_size][cdf_data][4B count][4B rans_size][rans_data]
     */
    static std::vector<uint8_t> encode_byte_stream(const std::vector<uint8_t>& bytes) {
        return byte_stream_encoder::encode_byte_stream(bytes);
    }

    // Shared/static-CDF variant for Mode5 payload (TileLZ bytes).
    // Format: [4B count][4B rans_size][rans_data]
    static std::vector<uint8_t> encode_byte_stream_shared_lz(const std::vector<uint8_t>& bytes) {
        return byte_stream_encoder::encode_byte_stream_shared_lz(bytes);
    }

private:
    // Note: get_mode5_shared_lz_cdf is now in byte_stream_encoder module
};


} // namespace hakonyans
