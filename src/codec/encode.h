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
#include <vector>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>

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
        if (use_cfl) header.flags |= 2;
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
        if (use_420) {
            int cb_w, cb_h; std::vector<uint8_t> cb_420, cr_420, y_ds;
            downsample_420(cb_plane.data(), width, height, cb_420, cb_w, cb_h);
            downsample_420(cr_plane.data(), width, height, cr_420, cb_w, cb_h);
            uint32_t pad_w_c = ((cb_w + 7) / 8) * 8, pad_h_c = ((cb_h + 7) / 8) * 8;
            if (use_cfl) { downsample_420(y_plane.data(), width, height, y_ds, cb_w, cb_h); }
            tile_cb = encode_plane(
                cb_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, quant_c,
                true, true, use_cfl ? &y_ds : nullptr, 0, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
            tile_cr = encode_plane(
                cr_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, quant_c,
                true, true, use_cfl ? &y_ds : nullptr, 1, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
        } else {
            tile_cb = encode_plane(
                cb_plane.data(), width, height, pad_w_y, pad_h_y, quant_c,
                true, true, use_cfl ? &y_plane : nullptr, 0, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
            tile_cr = encode_plane(
                cr_plane.data(), width, height, pad_w_y, pad_h_y, quant_c,
                true, true, use_cfl ? &y_plane : nullptr, 1, nullptr, nullptr,
                enable_screen_profile, use_band_group_cdf, target_pi_meta_ratio
            );
        }
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
        std::vector<uint8_t> padded = pad_image(pixels, width, height, pad_w, pad_h);
        std::vector<uint8_t> y_padded; if (y_ref) y_padded = pad_image(y_ref->data(), (y_ref->size() > width*height/2 ? width : (width+1)/2), (y_ref->size() > width*height/2 ? height : (height+1)/2), pad_w, pad_h);
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;
        
        // Block Types handling
        std::vector<FileHeader::BlockType> block_types;
        if (block_types_in && block_types_in->size() == nb) {
            block_types = *block_types_in;
        } else {
            block_types.assign(nb, FileHeader::BlockType::DCT);
        }
        
        
        // std::vector<uint8_t> bt_data = encode_block_types(block_types); // Moved to after loop


        std::vector<std::vector<int16_t>> dct_blocks(nb, std::vector<int16_t>(64));
        std::vector<float> activities(nb); float total_activity = 0.0f;
        std::vector<CfLParams> cfl_params;
        
        std::vector<Palette> palettes;
        std::vector<std::vector<uint8_t>> palette_indices;
        
        std::vector<CopyParams> copy_ops;
        int copy_op_idx = 0;

        for (int i = 0; i < nb; i++) {
            int bx = i % nx, by = i / nx; int16_t block[64]; extract_block(padded.data(), pad_w, pad_h, bx, by, block);
            
            // Automatic Block Type Selection Logic
            // Priority: Forced Input -> Copy (Search) -> Palette (Check) -> DCT
            
            FileHeader::BlockType selected_type = FileHeader::BlockType::DCT;
            
            // 1. Check Forced Input
            if (block_types_in && i < (int)block_types_in->size()) {
                selected_type = (*block_types_in)[i];
            } else if (enable_screen_profile) {
                // Automatic selection only if Screen Profile is enabled
                // A. Try Copy
                // Limit search to mainly text/UI areas (high variance? or always try?)
                // Copy search is expensive. For V1, let's limit radius or try only if variance is high?
                // Actually, Palette/Copy are good for flat areas too.
                // Let's try Copy Search with small radius (e.g. 128 px)
                
                CopyParams cp;
                // Note: We should search in `padded` (original pixels)?
                // Ideally we search in `reconstructed` pixels for correctness (drift prevention).
                // But `dct_blocks` haven't been quantized/dequantized yet!
                // We are in the loop `for (int i=0; i<nb; i++)`.
                // Previous blocks `0..i-1` have been processed but their RECONSTRUCTED pixels are not stored yet?
                // `encode_plane` calculates `quantized` coeff in the NEXT loop.
                // So strict IntraBC is impossible in this single-pass structure without buffering.
                //
                // SOLUTION for Multi-Pass:
                // We need to reconstruct blocks as we go if we want to search in them.
                // OR: We assume "high quality" and search in Processed Original pixels.
                // Screen content is usually lossless-ish.
                // Let's search in `padded` (Original) for Step 4 V1.
                // This introduces slight mismatch (Encoder thinks match, Decoder sees reconstructed diff).
                // But for lossless-like settings (Q90+), it matches.
                
                int sad = IntraBCSearch::search(padded.data(), pad_w, pad_h, bx, by, 64, cp);
                if (sad == 0) { // Perfect match
                    selected_type = FileHeader::BlockType::COPY;
                    copy_ops.push_back(cp);
                    block_types[i] = selected_type;
                    continue; // Skip Palette/DCT
                }
                
                // B. Try Palette
                Palette p = PaletteExtractor::extract(block, 8);
                if (p.size > 0 && p.size <= 8) {
                    // Check error? PaletteExtractor as implemented is "Lossless if <= 8 colors"
                    // If it returned a palette, it means the block HAD <= 8 colors.
                    selected_type = FileHeader::BlockType::PALETTE;
                }
            }
            
            // Apply selection
            block_types[i] = selected_type;

            if (selected_type == FileHeader::BlockType::COPY) {
                 // Already handled above if auto-detected.
                 // But if forced via input, we need to push param.
                 if (copy_params_in && copy_op_idx < (int)copy_params_in->size()) {
                    copy_ops.push_back((*copy_params_in)[copy_op_idx++]);
                 } else {
                    // Fallback if forced but no param?
                    // Should rely on auto-search if forced but no param?
                    // For now assume forced input comes with params or we re-search?
                    // Let's re-search if param missing.
                    CopyParams cp;
                    IntraBCSearch::search(padded.data(), pad_w, pad_h, bx, by, 64, cp);
                    copy_ops.push_back(cp);
                 }
                 continue;
            } else if (selected_type == FileHeader::BlockType::PALETTE) {
                // Try to extract palette (redundant if we just did it, but safe)
                Palette p = PaletteExtractor::extract(block, 8);
                if (p.size > 0) {
                    palettes.push_back(p);
                    palette_indices.push_back(PaletteExtractor::map_indices(block, p));
                    continue; 
                } else {
                    // Start of fallback to DCT
                    block_types[i] = FileHeader::BlockType::DCT;
                }
            }
            
            // Fallthrough to DCT logic
            if (y_ref) {
                int16_t yb[64]; extract_block(y_padded.data(), pad_w, pad_h, bx, by, yb);
                uint8_t yu[64], cu[64]; for (int k=0; k<64; k++) { yu[k]=(uint8_t)(yb[k]+128); cu[k]=(uint8_t)(block[k]+128); }
                CfLParams p = compute_cfl_params(yu, cu, cu); float a = (chroma_idx==0?p.alpha_cb:p.alpha_cr), b = (chroma_idx==0?p.beta_cb:p.beta_cr);
                cfl_params.push_back({a, b, a, b});
                for (int k=0; k<64; k++) block[k] = (int16_t)std::clamp((int)cu[k] - (int)std::round(a*yu[k]+b), -128, 127);
            }
            int16_t dct_out[64], zigzag[64]; DCT::forward(block, dct_out); Zigzag::scan(dct_out, zigzag);
            std::memcpy(dct_blocks[i].data(), zigzag, 128);
            if (aq) { float act = QuantTable::calc_activity(&zigzag[1]); activities[i] = act; total_activity += act; }
        }
        float avg_activity = total_activity / nb;
        std::vector<Token> dc_tokens;
        std::vector<Token> ac_tokens;
        std::vector<Token> ac_low_tokens;
        std::vector<Token> ac_mid_tokens;
        std::vector<Token> ac_high_tokens;
        std::vector<int8_t> q_deltas; if (aq) q_deltas.reserve(nb);
        int16_t prev_dc = 0;
        for (int i = 0; i < nb; i++) {
            if (i < (int)block_types.size() && (block_types[i] == FileHeader::BlockType::PALETTE || block_types[i] == FileHeader::BlockType::COPY)) {
                // Skip DCT encoding for palette/copy blocks
                 continue;
            }

            float scale = 1.0f; if (aq) { scale = QuantTable::get_adaptive_scale(activities[i], avg_activity); int8_t delta = (int8_t)std::clamp((scale - 1.0f) * 50.0f, -127.0f, 127.0f); q_deltas.push_back(delta); scale = 1.0f + (delta / 50.0f); }
            int16_t quantized[64]; for (int k = 0; k < 64; k++) { int16_t coeff = dct_blocks[i][k]; uint16_t q_adj = std::max((uint16_t)1, (uint16_t)std::round(quant[k] * scale)); int sign = (coeff < 0) ? -1 : 1; quantized[k] = sign * ((std::abs(coeff) + q_adj / 2) / q_adj); }
            int16_t dc_diff = quantized[0] - prev_dc; prev_dc = quantized[0]; dc_tokens.push_back(Tokenizer::tokenize_dc(dc_diff));
            if (use_band_group_cdf) {
                tokenize_ac_band(quantized, BAND_LOW, ac_low_tokens);
                tokenize_ac_band(quantized, BAND_MID, ac_mid_tokens);
                tokenize_ac_band(quantized, BAND_HIGH, ac_high_tokens);
            } else {
                auto at = Tokenizer::tokenize_ac(&quantized[1]);
                ac_tokens.insert(ac_tokens.end(), at.begin(), at.end());
            }
        }
        
        std::vector<uint8_t> bt_data = encode_block_types(block_types);
        std::vector<uint8_t> pal_data = PaletteCodec::encode_palette_stream(palettes, palette_indices);
        std::vector<uint8_t> cpy_data = CopyCodec::encode_copy_stream(copy_ops);
        std::vector<uint8_t> pindex_data;
        auto dc_stream = encode_tokens(dc_tokens, build_cdf(dc_tokens));
        std::vector<uint8_t> tile_data;
        if (use_band_group_cdf) {
            auto ac_low_stream = encode_tokens(ac_low_tokens, build_cdf(ac_low_tokens));
            auto ac_mid_stream = encode_tokens(ac_mid_tokens, build_cdf(ac_mid_tokens));
            auto ac_high_stream = encode_tokens(ac_high_tokens, build_cdf(ac_high_tokens));
            // TileHeader v3 (lossy): 10 fields (40 bytes)
            uint32_t sz[10] = {
                (uint32_t)dc_stream.size(), 
                (uint32_t)ac_low_stream.size(), 
                (uint32_t)ac_mid_stream.size(),
                (uint32_t)ac_high_stream.size(),
                (uint32_t)pindex_data.size(), 
                (uint32_t)q_deltas.size(), 
                (uint32_t)cfl_params.size()*2,
                (uint32_t)bt_data.size(),
                (uint32_t)pal_data.size(), 
                (uint32_t)cpy_data.size()
            };
            tile_data.resize(40); std::memcpy(&tile_data[0], sz, 40);
            tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
            tile_data.insert(tile_data.end(), ac_low_stream.begin(), ac_low_stream.end());
            tile_data.insert(tile_data.end(), ac_mid_stream.begin(), ac_mid_stream.end());
            tile_data.insert(tile_data.end(), ac_high_stream.begin(), ac_high_stream.end());
            if (sz[4]>0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
            if (sz[5]>0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[5]); }
            if (sz[6]>0) { for (const auto& p : cfl_params) { tile_data.push_back((int8_t)std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f)); tile_data.push_back((uint8_t)std::clamp(p.beta_cb, 0.0f, 255.0f)); } }
            if (sz[7]>0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
            if (sz[8]>0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
            if (sz[9]>0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        } else {
            auto ac_stream = encode_tokens(
                ac_tokens, build_cdf(ac_tokens), pi ? &pindex_data : nullptr,
                target_pindex_meta_ratio_percent
            );
            // TileHeader v2 (legacy): 8 fields (32 bytes)
            uint32_t sz[8] = {
                (uint32_t)dc_stream.size(),
                (uint32_t)ac_stream.size(),
                (uint32_t)pindex_data.size(),
                (uint32_t)q_deltas.size(),
                (uint32_t)cfl_params.size() * 2,
                (uint32_t)bt_data.size(),
                (uint32_t)pal_data.size(),
                (uint32_t)cpy_data.size()
            };
            tile_data.resize(32); std::memcpy(&tile_data[0], sz, 32);
            tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
            tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
            if (sz[2] > 0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
            if (sz[3] > 0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[3]); }
            if (sz[4] > 0) { for (const auto& p : cfl_params) { tile_data.push_back((int8_t)std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f)); tile_data.push_back((uint8_t)std::clamp(p.beta_cb, 0.0f, 255.0f)); } }
            if (sz[5] > 0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
            if (sz[6] > 0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
            if (sz[7] > 0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        }
        return tile_data;
    }

    static CDFTable build_cdf(const std::vector<Token>& t) { std::vector<uint32_t> f(76, 1); for (const auto& x : t) { int sym = static_cast<int>(x.type); if (sym < 76) f[sym]++; } return CDFBuilder().build_from_freq(f); }
    static int calculate_pindex_interval(
        size_t token_count,
        size_t encoded_token_stream_bytes,
        int target_meta_ratio_percent = 2
    ) {
        if (token_count == 0 || encoded_token_stream_bytes == 0) return 4096;
        target_meta_ratio_percent = std::clamp(target_meta_ratio_percent, 1, 10);
        double target_meta_bytes = (double)encoded_token_stream_bytes * (double)target_meta_ratio_percent / 100.0;
        // P-Index serialization: 12-byte header + 40 bytes/checkpoint.
        double target_checkpoints = (target_meta_bytes - 12.0) / 40.0;
        if (target_checkpoints < 1.0) target_checkpoints = 1.0;
        double raw_interval = (double)token_count / target_checkpoints;
        int interval = (int)std::llround(raw_interval);
        interval = std::clamp(interval, 64, 4096);
        interval = ((interval + 7) / 8) * 8;  // PIndexBuilder expects 8-aligned token interval.
        return std::clamp(interval, 64, 4096);
    }

    static std::vector<uint8_t> encode_tokens(
        const std::vector<Token>& t,
        const CDFTable& c,
        std::vector<uint8_t>* out_pi = nullptr,
        int target_pindex_meta_ratio_percent = 2
    ) {
        std::vector<uint8_t> output; int alpha = c.alphabet_size; std::vector<uint8_t> cdf_data(alpha * 4);
        for (int i = 0; i < alpha; i++) { uint32_t f = c.freq[i]; std::memcpy(&cdf_data[i * 4], &f, 4); }
        uint32_t cdf_size = cdf_data.size(); output.resize(4); std::memcpy(output.data(), &cdf_size, 4); output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        uint32_t token_count = t.size(); size_t count_offset = output.size(); output.resize(count_offset + 4); std::memcpy(&output[count_offset], &token_count, 4);
        FlatInterleavedEncoder encoder; for (const auto& tok : t) encoder.encode_symbol(c, static_cast<uint8_t>(tok.type)); auto rb = encoder.finish();
        uint32_t rans_size = rb.size(); size_t rs_offset = output.size(); output.resize(rs_offset + 4); std::memcpy(&output[rs_offset], &rans_size, 4); output.insert(output.end(), rb.begin(), rb.end());
        std::vector<uint8_t> raw_data; uint32_t raw_count = 0;
        for (const auto& tok : t) if (tok.raw_bits_count > 0) { raw_data.push_back(tok.raw_bits_count); raw_data.push_back(tok.raw_bits & 0xFF); raw_data.push_back((tok.raw_bits >> 8) & 0xFF); raw_count++; }
        size_t rc_offset = output.size(); output.resize(rc_offset + 4); std::memcpy(&output[rc_offset], &raw_count, 4); output.insert(output.end(), raw_data.begin(), raw_data.end());
        if (out_pi) {
            int interval = calculate_pindex_interval(
                t.size(), output.size(), target_pindex_meta_ratio_percent
            );
            auto pindex = PIndexBuilder::build(rb, c, t.size(), (uint32_t)interval);
            *out_pi = PIndexCodec::serialize(pindex);
        }
        return output;
    }

    static std::vector<uint8_t> pad_image(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h) {
        std::vector<uint8_t> padded(pad_w * pad_h);
        for (uint32_t y = 0; y < pad_h; y++) for (uint32_t x = 0; x < pad_w; x++) padded[y * pad_w + x] = pixels[std::min(y, height - 1) * width + std::min(x, width - 1)];
        return padded;
    }
    static void extract_block(const uint8_t* pixels, uint32_t stride, uint32_t height, int bx, int by, int16_t block[64]) {
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) block[y * 8 + x] = static_cast<int16_t>(pixels[(by * 8 + y) * stride + (bx * 8 + x)]) - 128;
    }

public:
    static std::vector<uint8_t> encode_block_types(const std::vector<FileHeader::BlockType>& types) {
        std::vector<uint8_t> out;
        if (types.empty()) return out;
        
        size_t n = types.size();
        size_t i = 0;
        while (i < n) {
            uint8_t type = (uint8_t)types[i];
            size_t run = 1;
            while (i + run < n && run < 64 && (uint8_t)types[i + run] == type) {
                run++;
            }
            // Format: (Count - 1) << 2 | Type
            // Type: 2 bits (0-3)
            // Count: 6 bits (1-64)
            out.push_back((uint8_t)(((run - 1) << 2) | (type & 0x03)));
            i += run;
        }
        return out;
    }

    // ========================================================================
    // Lossless encoding
    // ========================================================================

    /**
     * Encode a grayscale image losslessly.
     */
    static std::vector<uint8_t> encode_lossless(const uint8_t* pixels, uint32_t width, uint32_t height) {
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

        auto tile_data = encode_plane_lossless(plane.data(), width, height);

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
        // RGB -> YCoCg-R
        std::vector<int16_t> y_plane(width * height);
        std::vector<int16_t> co_plane(width * height);
        std::vector<int16_t> cg_plane(width * height);

        for (uint32_t i = 0; i < width * height; i++) {
            rgb_to_ycocg_r(rgb_data[i * 3], rgb_data[i * 3 + 1], rgb_data[i * 3 + 2],
                            y_plane[i], co_plane[i], cg_plane[i]);
        }

        auto tile_y  = encode_plane_lossless(y_plane.data(), width, height);
        auto tile_co = encode_plane_lossless(co_plane.data(), width, height);
        auto tile_cg = encode_plane_lossless(cg_plane.data(), width, height);

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
    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height
    ) {
        // Pad dimensions to multiple of 8
        uint32_t pad_w = ((width + 7) / 8) * 8;
        uint32_t pad_h = ((height + 7) / 8) * 8;
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;

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

        struct LosslessModeParams {
            int palette_max_colors = 2;
            int palette_transition_limit = 63;
            int64_t palette_variance_limit = 1040384;
        } mode_params;

        for (int i = 0; i < nb; i++) {
            int bx = i % nx, by = i / nx;
            int cur_x = bx * 8;
            int cur_y = by * 8;

            int16_t block[64];
            int64_t sum = 0, sum_sq = 0;
            int transitions = 0;
            bool palette_range_ok = true;
            int unique_cnt = 0;

            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int idx = y * 8 + x;
                    int16_t v = padded[(cur_y + y) * pad_w + (cur_x + x)];
                    block[idx] = v;
                    sum += v;
                    sum_sq += (int64_t)v * (int64_t)v;
                    if (v < -128 || v > 127) palette_range_ok = false;
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
            if (palette_range_ok && unique_cnt <= mode_params.palette_max_colors) {
                palette_candidate = PaletteExtractor::extract(block, mode_params.palette_max_colors);
                if (palette_candidate.size > 0 && palette_candidate.size <= mode_params.palette_max_colors) {
                    bool transition_ok = (transitions <= mode_params.palette_transition_limit) || (palette_candidate.size <= 1);
                    bool variance_ok = variance_proxy <= mode_params.palette_variance_limit;
                    if (transition_ok && variance_ok) {
                        palette_found = true;
                        palette_index_candidate = PaletteExtractor::map_indices(block, palette_candidate);
                    }
                }
            }

            // Mode decision:
            //   Copy (exact) -> Palette (guarded) -> Filter.
            // Keep Copy-first behavior for screen content; guard against noisy palette blocks.
            FileHeader::BlockType best_mode = FileHeader::BlockType::DCT;
            if (copy_found) {
                best_mode = FileHeader::BlockType::COPY;
            } else if (palette_found) {
                // Guard only pathological two-color checker-like blocks.
                bool noisy_palette2 =
                    (palette_candidate.size == 2) &&
                    (transitions > mode_params.palette_transition_limit) &&
                    (variance_proxy > mode_params.palette_variance_limit);
                if (!noisy_palette2) best_mode = FileHeader::BlockType::PALETTE;
            }

            block_types[i] = best_mode;
            if (best_mode == FileHeader::BlockType::COPY) {
                copy_ops.push_back(copy_candidate);
            } else if (best_mode == FileHeader::BlockType::PALETTE) {
                palettes.push_back(palette_candidate);
                palette_indices.push_back(std::move(palette_index_candidate));
            }
            // Filter mode keeps default DCT tag.
        }

        // --- Step 2: Custom filtering (block-type aware, full image context) ---
        // For each row: select best filter (considering Filter-block pixels only),
        // compute residuals for Filter-block pixels only.
        // Prediction context uses original pixel values — Palette/Copy pixels
        // serve as perfect anchors for prediction.
        std::vector<uint8_t> filter_ids(pad_h);
        std::vector<int16_t> filter_residuals;

        for (uint32_t y = 0; y < pad_h; y++) {
            int by_row = y / 8;

            // Check if this row has any filter blocks
            bool has_filter = false;
            for (int bx = 0; bx < nx; bx++) {
                if (block_types[by_row * nx + bx] == FileHeader::BlockType::DCT) {
                    has_filter = true;
                    break;
                }
            }
            if (!has_filter) {
                filter_ids[y] = 0;
                continue;
            }

            // Try all 5 filters, pick one minimizing sum(|residual|) for filter-block pixels
            int best_f = 0;
            int64_t best_sum = INT64_MAX;
            for (int f = 0; f < 5; f++) {
                int64_t sum = 0;
                for (uint32_t x = 0; x < pad_w; x++) {
                    int bx_col = x / 8;
                    if (block_types[by_row * nx + bx_col] != FileHeader::BlockType::DCT) continue;
                    int16_t orig = padded[y * pad_w + x];
                    int16_t a = (x > 0) ? padded[y * pad_w + x - 1] : 0;
                    int16_t b = (y > 0) ? padded[(y - 1) * pad_w + x] : 0;
                    int16_t c = (x > 0 && y > 0) ? padded[(y - 1) * pad_w + x - 1] : 0;
                    int16_t pred;
                    switch (f) {
                        case 0: pred = 0; break;
                        case 1: pred = a; break;
                        case 2: pred = b; break;
                        case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                        case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                        default: pred = 0; break;
                    }
                    sum += std::abs((int)(orig - pred));
                }
                if (sum < best_sum) { best_sum = sum; best_f = f; }
            }
            filter_ids[y] = (uint8_t)best_f;

            // Emit residuals for filter-block pixels only
            for (uint32_t x = 0; x < pad_w; x++) {
                int bx_col = x / 8;
                if (block_types[by_row * nx + bx_col] != FileHeader::BlockType::DCT) continue;
                int16_t orig = padded[y * pad_w + x];
                int16_t a = (x > 0) ? padded[y * pad_w + x - 1] : 0;
                int16_t b = (y > 0) ? padded[(y - 1) * pad_w + x] : 0;
                int16_t c = (x > 0 && y > 0) ? padded[(y - 1) * pad_w + x - 1] : 0;
                int16_t pred;
                switch (best_f) {
                    case 0: pred = 0; break;
                    case 1: pred = a; break;
                    case 2: pred = b; break;
                    case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                    case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                    default: pred = 0; break;
                }
                filter_residuals.push_back(orig - pred);
            }
        }

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
            lo_stream = encode_byte_stream(lo_bytes);
            hi_stream = encode_byte_stream(hi_bytes);
        }

        // --- Step 4: Encode block types, palette, copy ---
        std::vector<uint8_t> bt_data = encode_block_types(block_types);
        std::vector<uint8_t> pal_data = PaletteCodec::encode_palette_stream(palettes, palette_indices);
        std::vector<uint8_t> cpy_data = CopyCodec::encode_copy_stream(copy_ops);

        // --- Step 5: Pack tile data (32-byte header) ---
        uint32_t hdr[8] = {
            (uint32_t)filter_ids.size(),
            (uint32_t)lo_stream.size(),
            (uint32_t)hi_stream.size(),
            filter_pixel_count,
            (uint32_t)bt_data.size(),
            (uint32_t)pal_data.size(),
            (uint32_t)cpy_data.size(),
            0  // reserved
        };

        std::vector<uint8_t> tile_data;
        tile_data.resize(32);
        std::memcpy(tile_data.data(), hdr, 32);
        tile_data.insert(tile_data.end(), filter_ids.begin(), filter_ids.end());
        tile_data.insert(tile_data.end(), lo_stream.begin(), lo_stream.end());
        tile_data.insert(tile_data.end(), hi_stream.begin(), hi_stream.end());
        if (!bt_data.empty()) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
        if (!pal_data.empty()) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
        if (!cpy_data.empty()) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        return tile_data;
    }


    /**
     * Encode a byte stream using rANS with data-adaptive CDF.
     * Format: [4B cdf_size][cdf_data][4B count][4B rans_size][rans_data]
     */
    static std::vector<uint8_t> encode_byte_stream(const std::vector<uint8_t>& bytes) {
        // Build frequency table (alphabet = 256)
        std::vector<uint32_t> freq(256, 1);  // Laplace smoothing
        for (uint8_t b : bytes) freq[b]++;

        CDFTable cdf = CDFBuilder().build_from_freq(freq);

        // Serialize CDF
        std::vector<uint8_t> cdf_data(256 * 4);
        for (int i = 0; i < 256; i++) {
            uint32_t f = cdf.freq[i];
            std::memcpy(&cdf_data[i * 4], &f, 4);
        }

        // Encode symbols
        FlatInterleavedEncoder encoder;
        for (uint8_t b : bytes) {
            encoder.encode_symbol(cdf, b);
        }
        auto rans_bytes = encoder.finish();

        // Pack: cdf_size + cdf + count + rans_size + rans
        std::vector<uint8_t> output;
        uint32_t cdf_size = (uint32_t)cdf_data.size();
        uint32_t count = (uint32_t)bytes.size();
        uint32_t rans_size = (uint32_t)rans_bytes.size();

        output.resize(4); std::memcpy(output.data(), &cdf_size, 4);
        output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        size_t off = output.size();
        output.resize(off + 4); std::memcpy(&output[off], &count, 4);
        off = output.size();
        output.resize(off + 4); std::memcpy(&output[off], &rans_size, 4);
        output.insert(output.end(), rans_bytes.begin(), rans_bytes.end());
        return output;
    }
};

} // namespace hakonyans
