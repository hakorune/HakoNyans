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
#include <limits>

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
    struct LosslessModeDebugStats {
        uint64_t total_blocks;
        uint64_t copy_candidates;
        uint64_t palette_candidates;
        uint64_t copy_palette_overlap;

        uint64_t copy_selected;
        uint64_t palette_selected;
        uint64_t filter_selected;
        uint64_t filter_med_selected;
        uint64_t tile4_selected;

        uint64_t est_copy_bits_sum;      // candidate blocks only
        uint64_t est_palette_bits_sum;   // candidate blocks only
        uint64_t est_filter_bits_sum;    // all blocks
        uint64_t est_selected_bits_sum;  // chosen mode only

        LosslessModeDebugStats() { reset(); }

        void reset() {
            total_blocks = 0;
            copy_candidates = 0;
            palette_candidates = 0;
            copy_palette_overlap = 0;
            copy_selected = 0;
            palette_selected = 0;
            filter_selected = 0;
            filter_med_selected = 0;
            tile4_selected = 0;
            est_copy_bits_sum = 0;
            est_palette_bits_sum = 0;
            est_filter_bits_sum = 0;
            est_selected_bits_sum = 0;
        }
    };

    static void reset_lossless_mode_debug_stats() {
        tl_lossless_mode_debug_stats_.reset();
    }

    static LosslessModeDebugStats get_lossless_mode_debug_stats() {
        return tl_lossless_mode_debug_stats_;
    }

private:
    inline static thread_local LosslessModeDebugStats tl_lossless_mode_debug_stats_;

public:
    // Heuristic profile for applying "photo-only" lossless mode biases.
    // Classify from sampled exact-copy hit rate on Y plane:
    //   - UI/Anime/Game: high copy-hit rate (repeated tiles/text)
    //   - Photo/Natural: low copy-hit rate
    static bool is_photo_like_lossless_profile(const int16_t* y_plane, uint32_t width, uint32_t height) {
        if (!y_plane || width == 0 || height == 0) return false;

        const int bx = (int)((width + 7) / 8);
        const int by = (int)((height + 7) / 8);
        if (bx * by < 64) return false; // avoid unstable decisions on tiny images

        const CopyParams kCopyCandidates[4] = {
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8)
        };

        auto sample_at = [&](int x, int y) -> int16_t {
            int sx = std::clamp(x, 0, (int)width - 1);
            int sy = std::clamp(y, 0, (int)height - 1);
            return y_plane[(size_t)sy * width + (size_t)sx];
        };

        const int step = 4; // sample every 4th block in X/Y
        int samples = 0;
        int copy_hits = 0;

        for (int yb = 0; yb < by; yb += step) {
            for (int xb = 0; xb < bx; xb += step) {
                int cur_x = xb * 8;
                int cur_y = yb * 8;
                bool hit = false;

                for (const auto& cand : kCopyCandidates) {
                    int src_x = cur_x + cand.dx;
                    int src_y = cur_y + cand.dy;
                    if (src_x < 0 || src_y < 0) continue;
                    if (!(src_y < cur_y || (src_y == cur_y && src_x < cur_x))) continue;

                    bool match = true;
                    for (int y = 0; y < 8 && match; y++) {
                        for (int x = 0; x < 8; x++) {
                            if (sample_at(cur_x + x, cur_y + y) != sample_at(src_x + x, src_y + y)) {
                                match = false;
                                break;
                            }
                        }
                    }
                    if (match) {
                        hit = true;
                        break;
                    }
                }

                if (hit) copy_hits++;
                samples++;
            }
        }

        if (samples < 32) return false;
        const double copy_hit_rate = (double)copy_hits / (double)samples;

        // Photo-like if exact-copy opportunities are relatively sparse.
        return copy_hit_rate < 0.80;
    }

    static uint32_t extract_tile_cfl_size(const std::vector<uint8_t>& tile_data, bool use_band_group_cdf) {
        if (use_band_group_cdf) {
            if (tile_data.size() < 40) return 0;
            uint32_t sz[10];
            std::memcpy(sz, tile_data.data(), 40);
            return sz[6];
        }
        if (tile_data.size() < 32) return 0;
        uint32_t sz[8];
        std::memcpy(sz, tile_data.data(), 32);
        return sz[4];
    }

    static std::vector<uint8_t> serialize_cfl_legacy(const std::vector<CfLParams>& cfl_params) {
        std::vector<uint8_t> out;
        if (cfl_params.empty()) return out;
        out.reserve(cfl_params.size() * 2);
        for (const auto& p : cfl_params) {
            int a_q6 = (int)std::lround(std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f));
            int b_center = (int)std::lround(std::clamp(p.beta_cb, 0.0f, 255.0f));
            // Legacy predictor: pred = a*y + b_legacy
            // Current centered model: pred = a*(y-128) + b_center
            int b_legacy = std::clamp(b_center - 2 * a_q6, 0, 255);
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(a_q6)));
            out.push_back(static_cast<uint8_t>(b_legacy));
        }
        return out;
    }

    static std::vector<uint8_t> serialize_cfl_adaptive(const std::vector<CfLParams>& cfl_params) {
        std::vector<uint8_t> out;
        if (cfl_params.empty()) return out;

        const int nb = (int)cfl_params.size();
        int applied_count = 0;
        for (const auto& p : cfl_params) {
            if (p.alpha_cr > 0.5f) applied_count++;
        }
        if (applied_count == 0) return out;

        const size_t mask_bytes = ((size_t)nb + 7) / 8;
        out.resize(mask_bytes, 0);
        out.reserve(mask_bytes + (size_t)applied_count * 2);
        for (int i = 0; i < nb; i++) {
            if (cfl_params[i].alpha_cr > 0.5f) {
                out[(size_t)i / 8] |= (uint8_t)(1u << (i % 8));
                int a_q6 = (int)std::lround(std::clamp(cfl_params[i].alpha_cb * 64.0f, -128.0f, 127.0f));
                int b = (int)std::lround(std::clamp(cfl_params[i].beta_cb, 0.0f, 255.0f));
                out.push_back(static_cast<uint8_t>(static_cast<int8_t>(a_q6)));
                out.push_back(static_cast<uint8_t>(b));
            }
        }
        return out;
    }

    static std::vector<uint8_t> build_cfl_payload(const std::vector<CfLParams>& cfl_params) {
        if (cfl_params.empty()) return {};
        bool any_applied = false;
        for (const auto& p : cfl_params) {
            if (p.alpha_cr > 0.5f) {
                any_applied = true;
                break;
            }
        }
        if (!any_applied) return {};
        // Keep legacy payload for wire compatibility with older decoders.
        return serialize_cfl_legacy(cfl_params);
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
            bool cfl_applied = false;
            int cfl_alpha_q8 = 0, cfl_beta = 128;

            if (y_ref) {
                int16_t yb[64]; extract_block(y_padded.data(), pad_w, pad_h, bx, by, yb);
                uint8_t yu[64], cu[64]; 
                int64_t mse_no_cfl = 0;
                for (int k=0; k<64; k++) { 
                    yu[k]=(uint8_t)(yb[k]+128); 
                    cu[k]=(uint8_t)(block[k]+128); 
                    int err = (int)cu[k] - 128;
                    mse_no_cfl += (int64_t)err * err;
                }
                
                int alpha_q8, beta;
                compute_cfl_block_adaptive(yu, cu, alpha_q8, beta);
                
                int64_t mse_cfl = 0;
                for (int k=0; k<64; k++) {
                    int p = (alpha_q8 * (yu[k] - 128) + 128) >> 8;
                    p += beta;
                    int err = (int)cu[k] - std::clamp(p, 0, 255);
                    mse_cfl += (int64_t)err * err;
                }

                // Adaptive Decision: MSE based.
                // 1024 margin means average error reduction of 4.0 per pixel.
                if (mse_cfl < mse_no_cfl - 1024) {
                    cfl_applied = true;
                    cfl_alpha_q8 = alpha_q8;
                    cfl_beta = beta;
                    for (int k=0; k<64; k++) {
                        int p = (cfl_alpha_q8 * (yu[k] - 128) + 128) >> 8;
                        p += cfl_beta;
                        block[k] = (int16_t)std::clamp((int)cu[k] - p, -128, 127);
                    }
                }
            }
            
            if (y_ref) {
                cfl_params.push_back({(float)cfl_alpha_q8/256.0f, (float)cfl_beta, cfl_applied ? 1.0f : 0.0f, 0.0f});
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

        std::vector<uint8_t> cfl_data = build_cfl_payload(cfl_params);

        auto dc_stream = encode_tokens(dc_tokens, build_cdf(dc_tokens));
        std::vector<uint8_t> tile_data;
        if (use_band_group_cdf) {
            constexpr size_t kBandPindexMinStreamBytes = 32 * 1024;  // avoid overhead on tiny AC bands
            std::vector<uint8_t> pindex_low, pindex_mid, pindex_high;
            auto ac_low_stream = encode_tokens(
                ac_low_tokens, build_cdf(ac_low_tokens), pi ? &pindex_low : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            auto ac_mid_stream = encode_tokens(
                ac_mid_tokens, build_cdf(ac_mid_tokens), pi ? &pindex_mid : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            auto ac_high_stream = encode_tokens(
                ac_high_tokens, build_cdf(ac_high_tokens), pi ? &pindex_high : nullptr,
                target_pindex_meta_ratio_percent, kBandPindexMinStreamBytes
            );
            pindex_data = serialize_band_pindex_blob(pindex_low, pindex_mid, pindex_high);
            // TileHeader v3 (lossy): 10 fields (40 bytes)
            uint32_t sz[10] = {
                (uint32_t)dc_stream.size(), 
                (uint32_t)ac_low_stream.size(), 
                (uint32_t)ac_mid_stream.size(),
                (uint32_t)ac_high_stream.size(),
                (uint32_t)pindex_data.size(), 
                (uint32_t)q_deltas.size(), 
                (uint32_t)cfl_data.size(),
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
            if (sz[6]>0) { tile_data.insert(tile_data.end(), cfl_data.begin(), cfl_data.end()); }
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
                (uint32_t)cfl_data.size(),
                (uint32_t)bt_data.size(),
                (uint32_t)pal_data.size(),
                (uint32_t)cpy_data.size()
            };
            tile_data.resize(32); std::memcpy(&tile_data[0], sz, 32);
            tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
            tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
            if (sz[2] > 0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
            if (sz[3] > 0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[3]); }
            if (sz[4] > 0) { tile_data.insert(tile_data.end(), cfl_data.begin(), cfl_data.end()); }
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

    static std::vector<uint8_t> serialize_band_pindex_blob(
        const std::vector<uint8_t>& low,
        const std::vector<uint8_t>& mid,
        const std::vector<uint8_t>& high
    ) {
        if (low.empty() && mid.empty() && high.empty()) return {};
        std::vector<uint8_t> out;
        out.resize(12);
        uint32_t low_sz = (uint32_t)low.size();
        uint32_t mid_sz = (uint32_t)mid.size();
        uint32_t high_sz = (uint32_t)high.size();
        std::memcpy(&out[0], &low_sz, 4);
        std::memcpy(&out[4], &mid_sz, 4);
        std::memcpy(&out[8], &high_sz, 4);
        out.insert(out.end(), low.begin(), low.end());
        out.insert(out.end(), mid.begin(), mid.end());
        out.insert(out.end(), high.begin(), high.end());
        return out;
    }

    static std::vector<uint8_t> encode_tokens(
        const std::vector<Token>& t,
        const CDFTable& c,
        std::vector<uint8_t>* out_pi = nullptr,
        int target_pindex_meta_ratio_percent = 2,
        size_t min_pindex_stream_bytes = 0
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
            if (t.empty() || output.size() < min_pindex_stream_bytes) {
                out_pi->clear();
            } else {
                int interval = calculate_pindex_interval(
                    t.size(), output.size(), target_pindex_meta_ratio_percent
                );
                auto pindex = PIndexBuilder::build(rb, c, t.size(), (uint32_t)interval);
                *out_pi = PIndexCodec::serialize(pindex);
            }
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

        bool photo_like = is_photo_like_lossless_profile(plane.data(), width, height);
        auto tile_data = encode_plane_lossless(plane.data(), width, height, photo_like);

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

        bool photo_like = is_photo_like_lossless_profile(y_plane.data(), width, height);
        auto tile_y  = encode_plane_lossless(y_plane.data(), width, height, photo_like);
        auto tile_co = encode_plane_lossless(co_plane.data(), width, height, photo_like);
        auto tile_cg = encode_plane_lossless(cg_plane.data(), width, height, photo_like);

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

    // ------------------------------------------------------------------------
    // Lossless mode bit estimators (coarse heuristics for mode decision only)
    // Units: 1 unit = 0.5 bits (scaled by 2)
    // ------------------------------------------------------------------------
    static int estimate_copy_bits(const CopyParams& cp, int tile_width, bool use_photo_mode_bias) {
        (void)tile_width;
        int bits2 = 4;  // block_type (2 bits * 2)
        int small_idx = CopyCodec::small_vector_index(cp);
        if (small_idx >= 0) {
            bits2 += 4;  // small-vector code (2 bits * 2)
            bits2 += 4;  // amortized stream/mode overhead (2 bits * 2)
        } else {
            bits2 += 64; // raw dx/dy payload fallback (32 bits * 2)
        }
        if (use_photo_mode_bias) {
            bits2 += 8; // +4 bits penalty in photo mode
        }
        return bits2;
    }

    static int estimate_palette_index_bits_per_pixel(int palette_size) {
        if (palette_size <= 1) return 0;
        if (palette_size <= 2) return 1;
        if (palette_size <= 4) return 2;
        return 3;
    }

    static int estimate_palette_bits(const Palette& p, int transitions) {
        if (p.size == 0) return std::numeric_limits<int>::max();
        int bits2 = 4;   // block_type (2 bits * 2)
        bits2 += 16;     // per-block palette header (8 bits * 2)
        bits2 += (int)p.size * 16; // grayscale palette values (8 bits * 2)

        if (p.size <= 1) return bits2;
        if (p.size == 2) {
            // 2-color blocks are mask-based in PaletteCodec.
            // Lower transitions usually compress better via dictionary reuse.
            bits2 += (transitions <= 24) ? 48 : 128; // (24/64 bits * 2)
            return bits2;
        }

        int bits_per_index = estimate_palette_index_bits_per_pixel((int)p.size);
        bits2 += 64 * bits_per_index * 2;
        return bits2;
    }

    static int estimate_filter_symbol_bits2(int abs_residual, bool use_photo_mode_bias) {
        if (abs_residual == 0) return use_photo_mode_bias ? 1 : 2;  // 0.5 bits (photo) / 1.0 bits (default)
        if (abs_residual <= 1) return 4;  // 2 bits * 2
        if (abs_residual <= 3) return 6;  // 3 bits * 2
        if (abs_residual <= 7) return 8;  // 4 bits * 2
        if (abs_residual <= 15) return 10; // 5 bits * 2
        if (abs_residual <= 31) return 12; // 6 bits * 2
        if (abs_residual <= 63) return 14; // 7 bits * 2
        if (abs_residual <= 127) return 16; // 8 bits * 2
        return 20; // 10 bits * 2
    }

    static int lossless_filter_candidates(bool use_photo_mode_bias) {
        // Keep MED only for photo-like profile to avoid UI/anime regressions.
        return use_photo_mode_bias ? LosslessFilter::FILTER_COUNT : LosslessFilter::FILTER_MED;
    }

    static int estimate_filter_bits(
        const int16_t* padded, uint32_t pad_w, uint32_t pad_h, int cur_x, int cur_y, bool use_photo_mode_bias
    ) {
        (void)pad_h;
        int best_bits2 = std::numeric_limits<int>::max();
        const int filter_count = lossless_filter_candidates(use_photo_mode_bias);
        for (int f = 0; f < filter_count; f++) {
            int bits2 = 4; // block_type (2 bits * 2)
            bits2 += 6;    // effective filter_id overhead (3 bits * 2)
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int px = cur_x + x;
                    int py = cur_y + y;
                    int16_t orig = padded[py * (int)pad_w + px];
                    int16_t a = (px > 0) ? padded[py * (int)pad_w + (px - 1)] : 0;
                    int16_t b = (py > 0) ? padded[(py - 1) * (int)pad_w + px] : 0;
                    int16_t c = (px > 0 && py > 0) ? padded[(py - 1) * (int)pad_w + (px - 1)] : 0;
                    int16_t pred = 0;
                    switch (f) {
                        case 0: pred = 0; break;
                        case 1: pred = a; break;
                        case 2: pred = b; break;
                        case 3: pred = (int16_t)(((int)a + (int)b) / 2); break;
                        case 4: pred = LosslessFilter::paeth_predictor(a, b, c); break;
                        case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                    }
                    int abs_r = std::abs((int)orig - (int)pred);
                    bits2 += estimate_filter_symbol_bits2(abs_r, use_photo_mode_bias);
                }
            }
            best_bits2 = std::min(best_bits2, bits2);
        }
        return best_bits2;
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
        const int16_t* data, uint32_t width, uint32_t height, bool use_photo_mode_bias = false
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

        const CopyParams kTileMatch4Candidates[16] = {
            CopyParams(-4, 0), CopyParams(0, -4), CopyParams(-4, -4), CopyParams(4, -4),
            CopyParams(-8, 0), CopyParams(0, -8), CopyParams(-8, -8), CopyParams(8, -8),
            CopyParams(-12, 0), CopyParams(0, -12), CopyParams(-12, -4), CopyParams(-4, -12),
            CopyParams(-16, 0), CopyParams(0, -16), CopyParams(-16, -4), CopyParams(-4, -16)
        };

        struct Tile4Result {
            uint8_t indices[4];
        };
        std::vector<Tile4Result> tile4_results;

        struct LosslessModeParams {
            int palette_max_colors = 2;
            int palette_transition_limit = 63;
            int64_t palette_variance_limit = 1040384;
        } mode_params;

        FileHeader::BlockType prev_mode = FileHeader::BlockType::DCT;

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
                padded.data(), pad_w, pad_h, cur_x, cur_y, use_photo_mode_bias
            );
            if (tile4_found) {
                tile4_bits2 = 36; // 2 bit mode + 4x4 bit indices = 18 bits (36 units)
            }
            if (copy_found) {
                copy_bits2 = estimate_copy_bits(copy_candidate, (int)pad_w, use_photo_mode_bias);
            }
            if (palette_found) {
                palette_bits2 = estimate_palette_bits(palette_candidate, transitions);
            }

            if (use_photo_mode_bias) {
                // P0: Mode Inertia (-2 bits = -4 units)
                if (tile4_found && prev_mode == FileHeader::BlockType::TILE_MATCH4) tile4_bits2 -= 4;
                if (copy_found && prev_mode == FileHeader::BlockType::COPY) copy_bits2 -= 4;
                if (palette_found && prev_mode == FileHeader::BlockType::PALETTE) palette_bits2 -= 4;
                if (prev_mode == FileHeader::BlockType::DCT) filter_bits2 -= 4;
            }

            auto& mode_stats = tl_lossless_mode_debug_stats_;
            mode_stats.total_blocks++;
            mode_stats.est_filter_bits_sum += (uint64_t)(filter_bits2 / 2);
            if (tile4_found) {
                // For telemetry we don't have tile4_candidates yet, let's just use existing ones for now
                // or I could add it.
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

            block_types[i] = best_mode;
            prev_mode = best_mode;
            if (best_mode == FileHeader::BlockType::TILE_MATCH4) {
                tile4_results.push_back(tile4_candidate);
                mode_stats.est_selected_bits_sum += (uint64_t)(tile4_bits2 / 2);
                mode_stats.tile4_selected++;
            } else if (best_mode == FileHeader::BlockType::COPY) {
                mode_stats.copy_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(copy_bits2 / 2);
                copy_ops.push_back(copy_candidate);
            } else if (best_mode == FileHeader::BlockType::PALETTE) {
                mode_stats.palette_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(palette_bits2 / 2);
                palettes.push_back(palette_candidate);
                palette_indices.push_back(std::move(palette_index_candidate));
            } else {
                mode_stats.filter_selected++;
                mode_stats.est_selected_bits_sum += (uint64_t)(filter_bits2 / 2);
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

            // Try all filters, pick one minimizing sum(|residual|) for filter-block pixels
            int best_f = 0;
            int64_t best_sum = INT64_MAX;
            const int filter_count = lossless_filter_candidates(use_photo_mode_bias);
            for (int f = 0; f < filter_count; f++) {
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
                        case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
                        default: pred = 0; break;
                    }
                    sum += std::abs((int)(orig - pred));
                }
                if (sum < best_sum) { best_sum = sum; best_f = f; }
            }
            filter_ids[y] = (uint8_t)best_f;
            if (best_f == 5) tl_lossless_mode_debug_stats_.filter_med_selected++;

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
                    case 5: pred = LosslessFilter::med_predictor(a, b, c); break;
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

        // --- Step 4: Encode block types, palette, copy, tile4 ---
        std::vector<uint8_t> bt_data = encode_block_types(block_types);
        std::vector<uint8_t> pal_data = PaletteCodec::encode_palette_stream(palettes, palette_indices);
        std::vector<uint8_t> cpy_data = CopyCodec::encode_copy_stream(copy_ops);
        std::vector<uint8_t> tile4_data;
        for (const auto& res : tile4_results) {
            tile4_data.push_back((uint8_t)((res.indices[1] << 4) | (res.indices[0] & 0x0F)));
            tile4_data.push_back((uint8_t)((res.indices[3] << 4) | (res.indices[2] & 0x0F)));
        }

        // --- Step 5: Pack tile data (32-byte header) ---
        uint32_t hdr[8] = {
            (uint32_t)filter_ids.size(),
            (uint32_t)lo_stream.size(),
            (uint32_t)hi_stream.size(),
            filter_pixel_count,
            (uint32_t)bt_data.size(),
            (uint32_t)pal_data.size(),
            (uint32_t)cpy_data.size(),
            (uint32_t)tile4_data.size() // used reserved hdr[7]
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
        if (!tile4_data.empty()) tile_data.insert(tile_data.end(), tile4_data.begin(), tile4_data.end());
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
