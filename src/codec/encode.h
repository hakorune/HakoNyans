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
    struct LosslessPresetPlan {
        bool route_compete_luma = true;
        bool route_compete_chroma = true;
        bool conservative_chroma_route_policy = false;
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
            plane.data(), width, height, profile, preset_plan.route_compete_luma, false
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
        auto run_plane_task = [width, height, profile](
            const int16_t* plane, bool enable_route_compete, bool conservative_chroma_policy
        ) -> PlaneEncodeTaskResult {
            using TaskClock = std::chrono::steady_clock;
            GrayscaleEncoder::reset_lossless_mode_debug_stats();
            const auto t0 = TaskClock::now();
            auto tile = GrayscaleEncoder::encode_plane_lossless(
                plane, width, height, profile, enable_route_compete, conservative_chroma_policy
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
                y_plane.data(), width, height, profile, enable_y_route_compete, false
            );
            const auto t_plane_y1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_y_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane_y1 - t_plane_y0).count();

            const auto t_plane_co0 = Clock::now();
            tile_co = encode_plane_lossless(
                co_plane.data(), width, height, profile,
                allow_chroma_route_compete, conservative_chroma_route_policy
            );
            const auto t_plane_co1 = Clock::now();
            tl_lossless_mode_debug_stats_.perf_encode_plane_co_ns +=
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t_plane_co1 - t_plane_co0).count();

            const auto t_plane_cg0 = Clock::now();
            tile_cg = encode_plane_lossless(
                cg_plane.data(), width, height, profile,
                allow_chroma_route_compete, conservative_chroma_route_policy
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

    static LosslessPresetPlan build_lossless_preset_plan(
        LosslessPreset preset, LosslessProfile profile
    ) {
        LosslessPresetPlan plan{};
        switch (preset) {
            case LosslessPreset::FAST:
                plan.route_compete_luma = false;
                plan.route_compete_chroma = false;
                plan.conservative_chroma_route_policy = false;
                break;
            case LosslessPreset::BALANCED:
                plan.route_compete_luma = true;
                plan.route_compete_chroma = route_compete_chroma_enabled();
                if (profile == LosslessProfile::PHOTO && !route_compete_photo_chroma_enabled()) {
                    plan.route_compete_chroma = false;
                }
                plan.conservative_chroma_route_policy =
                    parse_bool_env("HKN_ROUTE_COMPETE_CHROMA_CONSERVATIVE", false);
                break;
            case LosslessPreset::MAX:
                // Max mode favors compression: always evaluate route competition on all planes.
                plan.route_compete_luma = true;
                plan.route_compete_chroma = true;
                plan.conservative_chroma_route_policy = false;
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
        LosslessModeDebugStats* stats = nullptr
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
            stats
        );
    }

    static std::vector<uint8_t> encode_plane_lossless_natural_row_tile_padded(
        const int16_t* padded, uint32_t pad_w, uint32_t pad_h,
        LosslessModeDebugStats* stats = nullptr
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
            stats
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
            false);
    }

    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height,
        LosslessProfile profile = LosslessProfile::UI,
        bool enable_route_competition = true,
        bool conservative_chroma_route_policy = false
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
            filter_residuals
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
                }
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
            [&padded, pad_w, pad_h](const int16_t*, uint32_t, uint32_t) {
                return GrayscaleEncoder::encode_plane_lossless_natural_row_tile_padded(
                    padded.data(), pad_w, pad_h, &tl_lossless_mode_debug_stats_
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
