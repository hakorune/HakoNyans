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
#include <vector>
#include <cstring>
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

        auto dc_stream = encode_tokens(analysis.dc_tokens, build_cdf(analysis.dc_tokens));
        std::vector<uint8_t> tile_data;
        if (use_band_group_cdf) {
            constexpr size_t kBandPindexMinStreamBytes = 32 * 1024;
            std::vector<uint8_t> pindex_low, pindex_mid, pindex_high;
            auto ac_low_stream = encode_tokens(
                analysis.ac_low_tokens, build_cdf(analysis.ac_low_tokens), pi ? &pindex_low : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            auto ac_mid_stream = encode_tokens(
                analysis.ac_mid_tokens, build_cdf(analysis.ac_mid_tokens), pi ? &pindex_mid : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            auto ac_high_stream = encode_tokens(
                analysis.ac_high_tokens, build_cdf(analysis.ac_high_tokens), pi ? &pindex_high : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
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
            auto ac_stream = encode_tokens(
                analysis.ac_tokens, build_cdf(analysis.ac_tokens), pi ? &pindex_data : nullptr,
                target_pindex_meta_ratio_percent
            );
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
    static std::vector<uint8_t> encode_lossless(const uint8_t* pixels, uint32_t width, uint32_t height) {
        reset_lossless_mode_debug_stats();

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

        auto profile = classify_lossless_profile(plane.data(), width, height);
        auto tile_data = encode_plane_lossless(plane.data(), width, height, profile);

        // Build file: Header + ChunkDir + Tile
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
        return output;
    }

    /**
     * Encode a color image losslessly using YCoCg-R.
     */
    static std::vector<uint8_t> encode_color_lossless(const uint8_t* rgb_data, uint32_t width, uint32_t height) {
        reset_lossless_mode_debug_stats();

        // RGB -> YCoCg-R
        std::vector<int16_t> y_plane(width * height);
        std::vector<int16_t> co_plane(width * height);
        std::vector<int16_t> cg_plane(width * height);

        for (uint32_t i = 0; i < width * height; i++) {
            rgb_to_ycocg_r(rgb_data[i * 3], rgb_data[i * 3 + 1], rgb_data[i * 3 + 2],
                            y_plane[i], co_plane[i], cg_plane[i]);
        }

        auto profile = classify_lossless_profile(y_plane.data(), width, height);
        auto tile_y  = encode_plane_lossless(y_plane.data(), width, height, profile);
        auto tile_co = encode_plane_lossless(co_plane.data(), width, height, profile);
        auto tile_cg = encode_plane_lossless(cg_plane.data(), width, height, profile);

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

    static bool is_natural_like(const ScreenPreflightMetrics& m) {
        // Natural-like textures: many unique samples, short runs, non-trivial gradients.
        return (m.unique_sample >= 128) &&
               (m.avg_run_x100 <= 260) &&
               (m.mean_abs_diff_x100 >= 120);
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

    static std::vector<uint8_t> encode_plane_lossless_natural_row_tile(
        const int16_t* plane, uint32_t width, uint32_t height
    ) {
        return lossless_natural_route::encode_plane_lossless_natural_row_tile(
            plane, width, height,
            [](int16_t v) { return zigzag_encode_val(v); },
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream_shared_lz(bytes);
            },
            [](const std::vector<uint8_t>& bytes) {
                return GrayscaleEncoder::encode_byte_stream(bytes);
            }
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
            use_photo_mode_bias ? LosslessProfile::PHOTO : LosslessProfile::UI);
    }

    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height, LosslessProfile profile = LosslessProfile::UI
    ) {
        // Pad dimensions to multiple of 8
        uint32_t pad_w = ((width + 7) / 8) * 8;
        uint32_t pad_h = ((height + 7) / 8) * 8;
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;

        // Phase 9s-5: Telemetry
        if (profile == LosslessProfile::UI) tl_lossless_mode_debug_stats_.profile_ui_tiles++;
        else if (profile == LosslessProfile::ANIME) tl_lossless_mode_debug_stats_.profile_anime_tiles++;
        else tl_lossless_mode_debug_stats_.profile_photo_tiles++;

        // Pad the int16_t image
        std::vector<int16_t> padded(pad_w * pad_h, 0);
        for (uint32_t y = 0; y < pad_h; y++) {
            for (uint32_t x = 0; x < pad_w; x++) {
                padded[y * pad_w + x] = data[std::min(y, height - 1) * width + std::min(x, width - 1)];
            }
        }

        // --- Step 1: Block classification ---
        std::vector<FileHeader::BlockType> block_types(nb, FileHeader::BlockType::DCT); // DCT = Filter for lossless
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        std::vector<CopyParams> copy_ops;
        const CopyParams kLosslessCopyCandidates[4] = {
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
        };

        const CopyParams kTileMatch4Candidates[16] = {
            CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
            CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
            CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
        };
        // Use Tile4Result from lossless_tile4_codec module
        using Tile4Result = lossless_tile4_codec::Tile4Result;
        std::vector<Tile4Result> tile4_results;
        
        struct LosslessModeParams {
            int palette_max_colors = 2;
            int palette_transition_limit = 63;
            int64_t palette_variance_limit = 1040384;
        } mode_params;
        
        if (profile == LosslessProfile::UI) {
            mode_params.palette_max_colors = 8;
            mode_params.palette_transition_limit = 58;
            mode_params.palette_variance_limit = 2621440;
        } else if (profile == LosslessProfile::ANIME) {
            mode_params.palette_max_colors = 8; // Fixed from 12 to match Palette struct size
            mode_params.palette_transition_limit = 62;
            mode_params.palette_variance_limit = 4194304;
        }
        // PHOTO uses default (2 colors, tight variance)

        FileHeader::BlockType prev_mode = FileHeader::BlockType::DCT;

        for (int i = 0; i < nb; i++) {
            int bx = i % nx, by = i / nx;
            int cur_x = bx * 8;
            int cur_y = by * 8;

            int16_t block[64];
            int64_t sum = 0, sum_sq = 0;
            int transitions = 0;
            int palette_transitions = 0;
            int unique_cnt = 0;

            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int idx = y * 8 + x;
                    int16_t v = padded[(cur_y + y) * pad_w + (cur_x + x)];
                    block[idx] = v;
                    sum += v;
                    sum_sq += (int64_t)v * (int64_t)v;
                    if (idx > 0 && block[idx - 1] != v) transitions++;
                }
            }

            {
                int16_t vals[64];
                std::memcpy(vals, block, sizeof(vals));
                std::sort(vals, vals + 64);
                unique_cnt = 1;
                for (int k = 1; k < 64; k++) {
                    if (vals[k] != vals[k - 1]) unique_cnt++;
                }
            }

            int64_t variance_proxy = sum_sq - ((sum * sum) / 64); // 64 * variance

            // Copy candidate (exact match only).
            bool copy_found = false;
            CopyParams copy_candidate;
            if (i > 0) {
                for (const auto& cand : kLosslessCopyCandidates) {
                    int src_x = cur_x + cand.dx;
                    int src_y = cur_y + cand.dy;
                    if (src_x < 0 || src_y < 0) continue;
                    if (src_x + 7 >= (int)pad_w || src_y + 7 >= (int)pad_h) continue;
                    if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                    bool match = true;
                    for (int y = 0; y < 8 && match; y++) {
                        for (int x = 0; x < 8; x++) {
                            if (padded[(cur_y + y) * pad_w + (cur_x + x)] !=
                                padded[(src_y + y) * pad_w + (src_x + x)]) {
                                match = false;
                                break;
                            }
                        }
                    }
                    if (match) {
                        copy_found = true;
                        copy_candidate = cand;
                        break;
                    }
                }
            }

            // Palette candidate.
            bool palette_found = false;
            Palette palette_candidate;
            std::vector<uint8_t> palette_index_candidate;
            palette_transitions = transitions;
            if (unique_cnt <= mode_params.palette_max_colors) {
                palette_candidate = PaletteExtractor::extract(block, mode_params.palette_max_colors);
                if (palette_candidate.size > 0 && palette_candidate.size <= mode_params.palette_max_colors) {
                    bool transition_ok = (transitions <= mode_params.palette_transition_limit) || (palette_candidate.size <= 1);
                    bool variance_ok = variance_proxy <= mode_params.palette_variance_limit;
                    if (transition_ok && variance_ok) {
                        palette_found = true;
                        palette_index_candidate = PaletteExtractor::map_indices(block, palette_candidate);
                        palette_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (palette_index_candidate[(size_t)k] != palette_index_candidate[(size_t)k - 1]) {
                                palette_transitions++;
                            }
                        }
                    }
                }
            }

            // TileMatch4 candidate (4x4 x 4 quadrants)
            bool tile4_found = false;
            Tile4Result tile4_candidate;
            {
                int matches = 0;
                for (int q = 0; q < 4; q++) {
                    int qx = (q % 2) * 4;
                    int qy = (q / 2) * 4;
                    int cur_qx = cur_x + qx;
                    int cur_qy = cur_y + qy;

                    bool q_match_found = false;
                    for (int cand_idx = 0; cand_idx < 16; cand_idx++) {
                        const auto& cand = kTileMatch4Candidates[cand_idx];
                        int src_x = cur_qx + cand.dx;
                        int src_y = cur_qy + cand.dy;

                        // Bounds and causality check
                        if (src_x < 0 || src_y < 0 || src_x + 3 >= (int)pad_w || src_y + 3 >= (int)pad_h) continue;
                        if (!(src_y < cur_qy || (src_y == cur_qy && src_x < cur_qx))) continue;

                        bool match = true;
                        for (int dy = 0; dy < 4 && match; dy++) {
                            for (int dx = 0; dx < 4; dx++) {
                                if (padded[(cur_qy + dy) * pad_w + (cur_qx + dx)] !=
                                    padded[(src_y + dy) * pad_w + (src_x + dx)]) {
                                    match = false;
                                    break;
                                }
                            }
                        }
                        if (match) {
                            tile4_candidate.indices[q] = (uint8_t)cand_idx;
                            q_match_found = true;
                            break;
                        }
                    }
                    if (q_match_found) matches++;
                    else break;
                }
                if (matches == 4) tile4_found = true;
            }

            // Mode decision:
            // Choose the minimum estimated bits among TILE_MATCH4 / Copy / Palette / Filter.
            int tile4_bits2 = std::numeric_limits<int>::max();
            int copy_bits2 = std::numeric_limits<int>::max();
            int palette_bits2 = std::numeric_limits<int>::max();
            int filter_bits2 = estimate_filter_bits(
                padded.data(), pad_w, pad_h, cur_x, cur_y, profile
            );
            auto& mode_stats = tl_lossless_mode_debug_stats_;
            if (tile4_found) {
                tile4_bits2 = 36; // 2 bit mode + 4x4 bit indices = 18 bits (36 units)
            }
            if (copy_found) {
                copy_bits2 = estimate_copy_bits(copy_candidate, (int)pad_w, profile);
            }

            // Phase 9t-2: Palette rescue for UI/ANIME.
            // If strict palette gates reject a block but a palette still beats filter
            // by a clear margin, re-enable palette candidate for this block.
            if (!palette_found && profile != LosslessProfile::PHOTO && unique_cnt <= 8) {
                Palette rescue_palette = PaletteExtractor::extract(block, 8);
                if (rescue_palette.size > 0 && rescue_palette.size <= 8) {
                    mode_stats.palette_rescue_attempted++;
                    auto rescue_indices = PaletteExtractor::map_indices(block, rescue_palette);
                    int rescue_transitions = 0;
                    for (int k = 1; k < 64; k++) {
                        if (rescue_indices[(size_t)k] != rescue_indices[(size_t)k - 1]) {
                            rescue_transitions++;
                        }
                    }
                    int rescue_bits2 = estimate_palette_bits(rescue_palette, rescue_transitions, profile);
                    if (profile == LosslessProfile::ANIME &&
                        rescue_palette.size >= 2 && rescue_transitions <= 60) {
                        rescue_bits2 -= 24;
                    }
                    if (rescue_bits2 + 8 < filter_bits2) {
                        palette_found = true;
                        palette_candidate = rescue_palette;
                        palette_index_candidate = std::move(rescue_indices);
                        palette_transitions = rescue_transitions;
                        mode_stats.palette_rescue_adopted++;
                        mode_stats.palette_rescue_gain_bits_sum +=
                            (uint64_t)std::max(0, (filter_bits2 - rescue_bits2) / 2);
                    }
                }
            }

            if (palette_found) {
                palette_bits2 = estimate_palette_bits(
                    palette_candidate, palette_transitions, profile
                );
                // Phase 9s-6: Anime-specific palette bias
                if (profile == LosslessProfile::ANIME && palette_candidate.size >= 2 && palette_transitions <= 60) {
                    palette_bits2 -= 24;
                    tl_lossless_mode_debug_stats_.anime_palette_bonus_applied++;
                }

                // Phase 9t-2: Palette rescue bias for UI/ANIME-like flat regions.
                // Guarded by high variance proxy to avoid Photo/Natural regressions.
                const bool rescue_bias_cond =
                    (profile != LosslessProfile::PHOTO) &&
                    (palette_candidate.size <= 8) &&
                    (unique_cnt <= 8) &&
                    (palette_transitions <= 32) &&
                    (variance_proxy >= 30000);
                if (rescue_bias_cond) {
                    mode_stats.palette_rescue_attempted++;
                    palette_bits2 -= 32; // 16-bit rescue bias
                }
            }

            if (profile == LosslessProfile::PHOTO) {
                // P0: Mode Inertia (-2 bits = -4 units)
                if (tile4_found && prev_mode == FileHeader::BlockType::TILE_MATCH4) tile4_bits2 -= 4;
                if (copy_found && prev_mode == FileHeader::BlockType::COPY) copy_bits2 -= 4;
                if (palette_found && prev_mode == FileHeader::BlockType::PALETTE) palette_bits2 -= 4;
                if (prev_mode == FileHeader::BlockType::DCT) filter_bits2 -= 4;
            }

            mode_stats.total_blocks++;
            mode_stats.est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);
            if (tile4_found) {
                mode_stats.tile4_candidates++;
                mode_stats.est_tile4_bits_sum += (uint64_t)(tile4_bits2 / 2);
            }
            if (copy_found) {
                mode_stats.copy_candidates++;
                mode_stats.est_copy_bits_sum += (uint64_t)(copy_bits2 / 2);
            }
            if (palette_found) {
                mode_stats.palette_candidates++;
                mode_stats.est_palette_bits_sum += (uint64_t)(palette_bits2 / 2);
            }
            if (copy_found && palette_found) mode_stats.copy_palette_overlap++;

            FileHeader::BlockType best_mode = FileHeader::BlockType::DCT;
            if (tile4_bits2 <= copy_bits2 && tile4_bits2 <= palette_bits2 && tile4_bits2 <= filter_bits2) {
                best_mode = FileHeader::BlockType::TILE_MATCH4;
            } else if (copy_bits2 <= palette_bits2 && copy_bits2 <= filter_bits2) {
                best_mode = FileHeader::BlockType::COPY;
            } else if (palette_bits2 <= filter_bits2) {
                best_mode = FileHeader::BlockType::PALETTE;
            }
            int selected_bits2 = filter_bits2;
            if (best_mode == FileHeader::BlockType::TILE_MATCH4) selected_bits2 = tile4_bits2;
            else if (best_mode == FileHeader::BlockType::COPY) selected_bits2 = copy_bits2;
            else if (best_mode == FileHeader::BlockType::PALETTE) selected_bits2 = palette_bits2;

            // Diagnostics: candidate existed but lost to another mode.
            if (tile4_found && best_mode != FileHeader::BlockType::TILE_MATCH4) {
                if (best_mode == FileHeader::BlockType::COPY) mode_stats.tile4_rejected_by_copy++;
                else if (best_mode == FileHeader::BlockType::PALETTE) mode_stats.tile4_rejected_by_palette++;
                else mode_stats.tile4_rejected_by_filter++;
                mode_stats.est_tile4_loss_bits_sum +=
                    (uint64_t)(std::max(0, tile4_bits2 - selected_bits2) / 2);
            }
            if (copy_found && best_mode != FileHeader::BlockType::COPY) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) mode_stats.copy_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::PALETTE) mode_stats.copy_rejected_by_palette++;
                else mode_stats.copy_rejected_by_filter++;
                mode_stats.est_copy_loss_bits_sum +=
                    (uint64_t)(std::max(0, copy_bits2 - selected_bits2) / 2);
            }
            if (palette_found && best_mode != FileHeader::BlockType::PALETTE) {
                if (best_mode == FileHeader::BlockType::TILE_MATCH4) mode_stats.palette_rejected_by_tile4++;
                else if (best_mode == FileHeader::BlockType::COPY) mode_stats.palette_rejected_by_copy++;
                else mode_stats.palette_rejected_by_filter++;
                mode_stats.est_palette_loss_bits_sum +=
                    (uint64_t)(std::max(0, palette_bits2 - selected_bits2) / 2);
            }

            block_types[i] = best_mode;
            prev_mode = best_mode;
            if (best_mode == FileHeader::BlockType::TILE_MATCH4) {
                tile4_results.push_back(tile4_candidate);
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                mode_stats.tile4_selected++;
            } else if (best_mode == FileHeader::BlockType::COPY) {
                mode_stats.copy_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                copy_ops.push_back(copy_candidate);
            } else if (best_mode == FileHeader::BlockType::PALETTE) {
                mode_stats.palette_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);
                palettes.push_back(palette_candidate);
                palette_indices.push_back(std::move(palette_index_candidate));
                // Count rescue adoption for biased palette paths.
                if (profile != LosslessProfile::PHOTO &&
                    palette_candidate.size <= 8 &&
                    unique_cnt <= 8 &&
                    palette_transitions <= 32 &&
                    variance_proxy >= 30000) {
                    mode_stats.palette_rescue_adopted++;
                    mode_stats.palette_rescue_gain_bits_sum += 16;
                }
            } else {
                mode_stats.filter_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(selected_bits2 / 2);

                // Phase 9t-1: collect detailed diagnostics for filter-selected blocks.
                if (copy_found) mode_stats.filter_blocks_with_copy_candidate++;
                if (palette_found) mode_stats.filter_blocks_with_palette_candidate++;
                if (unique_cnt <= 2) mode_stats.filter_blocks_unique_le2++;
                else if (unique_cnt <= 4) mode_stats.filter_blocks_unique_le4++;
                else if (unique_cnt <= 8) mode_stats.filter_blocks_unique_le8++;
                else mode_stats.filter_blocks_unique_gt8++;
                mode_stats.filter_blocks_transitions_sum += (uint64_t)transitions;
                mode_stats.filter_blocks_variance_proxy_sum +=
                    (uint64_t)std::max<int64_t>(0, variance_proxy);
                mode_stats.filter_blocks_est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);

                // Diagnose whether palette(<=8, current palette struct limit)
                // could beat filter on this block.
                if (unique_cnt <= 8) {
                    Palette diag_palette16 = PaletteExtractor::extract(block, 8);
                    if (diag_palette16.size > 0 && diag_palette16.size <= 8) {
                        auto diag_indices = PaletteExtractor::map_indices(block, diag_palette16);
                        int diag_transitions = 0;
                        for (int k = 1; k < 64; k++) {
                            if (diag_indices[(size_t)k] != diag_indices[(size_t)k - 1]) {
                                diag_transitions++;
                            }
                        }
                        int diag_palette_bits2 = estimate_palette_bits(diag_palette16, diag_transitions, profile);
                        if (profile == LosslessProfile::ANIME &&
                            diag_palette16.size >= 2 && diag_transitions <= 60) {
                            diag_palette_bits2 -= 24;
                        }
                        mode_stats.filter_diag_palette16_candidates++;
                        mode_stats.filter_diag_palette16_size_sum += diag_palette16.size;
                        mode_stats.filter_diag_palette16_est_bits_sum += (uint64_t)(diag_palette_bits2 / 2);
                        if (diag_palette_bits2 < filter_bits2) {
                            mode_stats.filter_diag_palette16_better++;
                            mode_stats.filter_diag_palette16_gain_bits_sum +=
                                (uint64_t)((filter_bits2 - diag_palette_bits2) / 2);
                        }
                    }
                }
            }
            // Filter mode keeps default DCT tag.
        }
        // --- Step 2: Custom filtering (block-type aware, full image context) ---
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

            // --- Phase 9n: filter_hi sparse-or-dense wrapper ---
            hi_stream = filter_hi_wrapper::encode_filter_hi_stream(
                hi_bytes, &tl_lossless_mode_debug_stats_
            );
        }

        // --- Step 4: Encode block types, palette, copy, tile4 ---
        // --- Step 4: Encode block types, palette, copy, tile4 ---
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

        // --- Step 5: Compress filter_ids (Phase 9n) ---
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

        // --- Step 6: Pack tile data (32-byte header) ---
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
        return lossless_route_competition::choose_best_tile(
            tile_data,
            data,
            width,
            height,
            static_cast<int>(profile),
            &tl_lossless_mode_debug_stats_,
            [](const int16_t* p, uint32_t w, uint32_t h) {
                return GrayscaleEncoder::analyze_screen_indexed_preflight(p, w, h);
            },
            [](const int16_t* p, uint32_t w, uint32_t h, ScreenBuildFailReason* fail_reason) {
                return GrayscaleEncoder::encode_plane_lossless_screen_indexed_tile(p, w, h, fail_reason);
            },
            [](const ScreenPreflightMetrics& m) {
                return GrayscaleEncoder::is_natural_like(m);
            },
            [](const int16_t* p, uint32_t w, uint32_t h) {
                return GrayscaleEncoder::encode_plane_lossless_natural_row_tile(p, w, h);
            }
        );
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
