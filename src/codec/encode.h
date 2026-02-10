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
        auto tile_data = encode_plane(pixels, width, height, pad_w, pad_h, quant, true, true);
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
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 3; header.colorspace = 0; header.subsampling = use_420 ? 1 : 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        if (use_cfl) header.flags |= 2;
        uint16_t quant[64]; QuantTable::build_quant_table(quality, quant);
        uint32_t pad_w_y = header.padded_width(), pad_h_y = header.padded_height();
        auto tile_y = encode_plane(y_plane.data(), width, height, pad_w_y, pad_h_y, quant, true, true, nullptr, 0, nullptr, nullptr, enable_screen_profile);
        std::vector<uint8_t> tile_cb, tile_cr;
        if (use_420) {
            int cb_w, cb_h; std::vector<uint8_t> cb_420, cr_420, y_ds;
            downsample_420(cb_plane.data(), width, height, cb_420, cb_w, cb_h);
            downsample_420(cr_plane.data(), width, height, cr_420, cb_w, cb_h);
            uint32_t pad_w_c = ((cb_w + 7) / 8) * 8, pad_h_c = ((cb_h + 7) / 8) * 8;
            if (use_cfl) { downsample_420(y_plane.data(), width, height, y_ds, cb_w, cb_h); }
            tile_cb = encode_plane(cb_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, quant, true, true, use_cfl ? &y_ds : nullptr, 0, nullptr, nullptr, enable_screen_profile);
            tile_cr = encode_plane(cr_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, quant, true, true, use_cfl ? &y_ds : nullptr, 1, nullptr, nullptr, enable_screen_profile);
        } else {
            tile_cb = encode_plane(cb_plane.data(), width, height, pad_w_y, pad_h_y, quant, true, true, use_cfl ? &y_plane : nullptr, 0, nullptr, nullptr, enable_screen_profile);
            tile_cr = encode_plane(cr_plane.data(), width, height, pad_w_y, pad_h_y, quant, true, true, use_cfl ? &y_plane : nullptr, 1, nullptr, nullptr, enable_screen_profile);
        }
        QMATChunk qmat; qmat.quality = quality; std::memcpy(qmat.quant_y, quant, 128); auto qmat_data = qmat.serialize();
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
        bool enable_screen_profile = false
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
        std::vector<Token> dc_tokens; std::vector<Token> ac_tokens;
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
            auto at = Tokenizer::tokenize_ac(&quantized[1]); ac_tokens.insert(ac_tokens.end(), at.begin(), at.end());
        }
        
        std::vector<uint8_t> bt_data = encode_block_types(block_types);
        std::vector<uint8_t> pal_data = PaletteCodec::encode_palette_stream(palettes, palette_indices);
        std::vector<uint8_t> cpy_data = CopyCodec::encode_copy_stream(copy_ops);
        std::vector<uint8_t> pindex_data;
        auto dc_stream = encode_tokens(dc_tokens, build_cdf(dc_tokens));
        auto ac_stream = encode_tokens(ac_tokens, build_cdf(ac_tokens), pi ? &pindex_data : nullptr);
        std::vector<uint8_t> tile_data;
        // TileHeader v2: 8 fields (32 bytes)
        uint32_t sz[8] = {
            (uint32_t)dc_stream.size(), 
            (uint32_t)ac_stream.size(), 
            (uint32_t)pindex_data.size(), 
            (uint32_t)q_deltas.size(), 
            (uint32_t)cfl_params.size()*2,
            (uint32_t)bt_data.size(),
            (uint32_t)pal_data.size(), 
            (uint32_t)cpy_data.size()
        };
        tile_data.resize(32); std::memcpy(&tile_data[0], sz, 32);
        tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
        tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
        if (sz[2]>0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
        if (sz[3]>0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[3]); }
        if (sz[4]>0) { for (const auto& p : cfl_params) { tile_data.push_back((int8_t)std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f)); tile_data.push_back((uint8_t)std::clamp(p.beta_cb, 0.0f, 255.0f)); } }
        if (sz[5]>0) tile_data.insert(tile_data.end(), bt_data.begin(), bt_data.end());
        if (sz[6]>0) tile_data.insert(tile_data.end(), pal_data.begin(), pal_data.end());
        if (sz[7]>0) tile_data.insert(tile_data.end(), cpy_data.begin(), cpy_data.end());
        return tile_data;
    }

    static CDFTable build_cdf(const std::vector<Token>& t) { std::vector<uint32_t> f(76, 1); for (const auto& x : t) { int sym = static_cast<int>(x.type); if (sym < 76) f[sym]++; } return CDFBuilder().build_from_freq(f); }
    static std::vector<uint8_t> encode_tokens(const std::vector<Token>& t, const CDFTable& c, std::vector<uint8_t>* out_pi = nullptr) {
        std::vector<uint8_t> output; int alpha = c.alphabet_size; std::vector<uint8_t> cdf_data(alpha * 4);
        for (int i = 0; i < alpha; i++) { uint32_t f = c.freq[i]; std::memcpy(&cdf_data[i * 4], &f, 4); }
        uint32_t cdf_size = cdf_data.size(); output.resize(4); std::memcpy(output.data(), &cdf_size, 4); output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        uint32_t token_count = t.size(); size_t count_offset = output.size(); output.resize(count_offset + 4); std::memcpy(&output[count_offset], &token_count, 4);
        FlatInterleavedEncoder encoder; for (const auto& tok : t) encoder.encode_symbol(c, static_cast<uint8_t>(tok.type)); auto rb = encoder.finish();
        if (out_pi) { auto pindex = PIndexBuilder::build(rb, c, t.size(), 1024); *out_pi = PIndexCodec::serialize(pindex); }
        uint32_t rans_size = rb.size(); size_t rs_offset = output.size(); output.resize(rs_offset + 4); std::memcpy(&output[rs_offset], &rans_size, 4); output.insert(output.end(), rb.begin(), rb.end());
        std::vector<uint8_t> raw_data; uint32_t raw_count = 0;
        for (const auto& tok : t) if (tok.raw_bits_count > 0) { raw_data.push_back(tok.raw_bits_count); raw_data.push_back(tok.raw_bits & 0xFF); raw_data.push_back((tok.raw_bits >> 8) & 0xFF); raw_count++; }
        size_t rc_offset = output.size(); output.resize(rc_offset + 4); std::memcpy(&output[rc_offset], &raw_count, 4); output.insert(output.end(), raw_data.begin(), raw_data.end());
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
     * Encode a single int16_t plane losslessly.
     * Pipeline: filter -> zigzag -> split lo/hi bytes -> rANS encode each stream.
     */
    static std::vector<uint8_t> encode_plane_lossless(
        const int16_t* data, uint32_t width, uint32_t height
    ) {
        // Step 1: Apply prediction filter
        std::vector<uint8_t> filter_ids;
        std::vector<int16_t> filtered;
        LosslessFilter::filter_image(data, width, height, filter_ids, filtered);

        // Step 2: ZigZag encode residuals (int16_t -> uint16_t)
        size_t total = width * height;
        std::vector<uint8_t> lo_bytes(total), hi_bytes(total);
        for (size_t i = 0; i < total; i++) {
            uint16_t zz = zigzag_encode_val(filtered[i]);
            lo_bytes[i] = (uint8_t)(zz & 0xFF);
            hi_bytes[i] = (uint8_t)((zz >> 8) & 0xFF);
        }

        // Step 3: rANS encode each byte stream
        auto lo_stream = encode_byte_stream(lo_bytes);
        auto hi_stream = encode_byte_stream(hi_bytes);

        // Step 4: Pack tile data
        // Header: 4 x uint32_t = 16 bytes
        uint32_t hdr[4] = {
            (uint32_t)filter_ids.size(),
            (uint32_t)lo_stream.size(),
            (uint32_t)hi_stream.size(),
            0  // pindex (reserved)
        };

        std::vector<uint8_t> tile_data;
        tile_data.resize(16);
        std::memcpy(tile_data.data(), hdr, 16);
        tile_data.insert(tile_data.end(), filter_ids.begin(), filter_ids.end());
        tile_data.insert(tile_data.end(), lo_stream.begin(), lo_stream.end());
        tile_data.insert(tile_data.end(), hi_stream.begin(), hi_stream.end());
        return tile_data;
    }

    /**
     * Encode a byte stream using rANS (for lossless mode).
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

