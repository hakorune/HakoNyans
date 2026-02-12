#pragma once

#include "../entropy/nyans_p/tokenization_v2.h"
#include "band_groups.h"
#include "colorspace.h"
#include "copy.h"
#include "headers.h"
#include "lossy_image_helpers.h"
#include "palette.h"
#include "quant.h"
#include "transform_dct.h"
#include "zigzag.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hakonyans::lossy_plane_analysis {

struct AnalysisResult {
    std::vector<FileHeader::BlockType> block_types;
    std::vector<Palette> palettes;
    std::vector<std::vector<uint8_t>> palette_indices;
    std::vector<CopyParams> copy_ops;
    std::vector<CfLParams> cfl_params;

    std::vector<Token> dc_tokens;
    std::vector<Token> ac_tokens;
    std::vector<Token> ac_low_tokens;
    std::vector<Token> ac_mid_tokens;
    std::vector<Token> ac_high_tokens;
    std::vector<int8_t> q_deltas;
};

inline AnalysisResult analyze_blocks_and_tokenize(
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t pad_w,
    uint32_t pad_h,
    const uint16_t quant[64],
    bool aq,
    const std::vector<uint8_t>* y_ref,
    const std::vector<FileHeader::BlockType>* block_types_in,
    const std::vector<CopyParams>* copy_params_in,
    bool enable_screen_profile,
    bool use_band_group_cdf
) {
    AnalysisResult out;

    std::vector<uint8_t> padded =
        lossy_image_helpers::pad_image(pixels, width, height, pad_w, pad_h);
    std::vector<uint8_t> y_padded;
    if (y_ref) {
        y_padded = lossy_image_helpers::pad_image(
            y_ref->data(),
            (y_ref->size() > width * height / 2 ? width : (width + 1) / 2),
            (y_ref->size() > width * height / 2 ? height : (height + 1) / 2),
            pad_w,
            pad_h
        );
    }

    const int nx = (int)(pad_w / 8);
    const int ny = (int)(pad_h / 8);
    const int nb = nx * ny;

    if (block_types_in && block_types_in->size() == (size_t)nb) {
        out.block_types = *block_types_in;
    } else {
        out.block_types.assign(nb, FileHeader::BlockType::DCT);
    }

    std::vector<std::vector<int16_t>> dct_blocks((size_t)nb, std::vector<int16_t>(64));
    std::vector<float> activities((size_t)nb);
    float total_activity = 0.0f;
    int copy_op_idx = 0;

    for (int i = 0; i < nb; i++) {
        int bx = i % nx;
        int by = i / nx;
        int16_t block[64];
        lossy_image_helpers::extract_block(padded.data(), pad_w, pad_h, bx, by, block);

        FileHeader::BlockType selected_type = FileHeader::BlockType::DCT;

        if (block_types_in && i < (int)block_types_in->size()) {
            selected_type = (*block_types_in)[(size_t)i];
        } else if (enable_screen_profile) {
            CopyParams cp;
            int sad = IntraBCSearch::search(padded.data(), pad_w, pad_h, bx, by, 64, cp);
            if (sad == 0) {
                selected_type = FileHeader::BlockType::COPY;
                out.copy_ops.push_back(cp);
                out.block_types[(size_t)i] = selected_type;
                continue;
            }

            Palette p = PaletteExtractor::extract(block, 8);
            if (p.size > 0 && p.size <= 8) {
                selected_type = FileHeader::BlockType::PALETTE;
            }
        }

        out.block_types[(size_t)i] = selected_type;

        if (selected_type == FileHeader::BlockType::COPY) {
            if (copy_params_in && copy_op_idx < (int)copy_params_in->size()) {
                out.copy_ops.push_back((*copy_params_in)[(size_t)copy_op_idx++]);
            } else {
                CopyParams cp;
                IntraBCSearch::search(padded.data(), pad_w, pad_h, bx, by, 64, cp);
                out.copy_ops.push_back(cp);
            }
            if (y_ref) out.cfl_params.push_back({0.0f, 128.0f, 0.0f, 0.0f});
            continue;
        } else if (selected_type == FileHeader::BlockType::PALETTE) {
            Palette p = PaletteExtractor::extract(block, 8);
            if (p.size > 0) {
                out.palettes.push_back(p);
                out.palette_indices.push_back(PaletteExtractor::map_indices(block, p));
                if (y_ref) out.cfl_params.push_back({0.0f, 128.0f, 0.0f, 0.0f});
                continue;
            } else {
                out.block_types[(size_t)i] = FileHeader::BlockType::DCT;
            }
        }

        bool cfl_applied = false;
        int cfl_alpha_q8 = 0;
        int cfl_beta = 128;

        if (y_ref) {
            int16_t yb[64];
            lossy_image_helpers::extract_block(y_padded.data(), pad_w, pad_h, bx, by, yb);
            uint8_t yu[64], cu[64];
            int64_t mse_no_cfl = 0;
            for (int k = 0; k < 64; k++) {
                yu[k] = (uint8_t)(yb[k] + 128);
                cu[k] = (uint8_t)(block[k] + 128);
                int err = (int)cu[k] - 128;
                mse_no_cfl += (int64_t)err * err;
            }

            int alpha_q8, beta;
            compute_cfl_block_adaptive(yu, cu, alpha_q8, beta);

            int a_q6 = (int)std::lround(std::clamp((float)alpha_q8 / 256.0f * 64.0f, -128.0f, 127.0f));
            int b_center = (int)std::lround(std::clamp((float)beta, 0.0f, 255.0f));

            int64_t mse_cfl_recon = 0;
            for (int k = 0; k < 64; k++) {
                int p = (a_q6 * (yu[k] - 128) + 32) >> 6;
                p += b_center;
                int err = (int)cu[k] - std::clamp(p, 0, 255);
                mse_cfl_recon += (int64_t)err * err;
            }

            if (mse_cfl_recon < mse_no_cfl - 1024) {
                cfl_applied = true;
                cfl_alpha_q8 = (a_q6 * 256) / 64;
                cfl_beta = b_center;

                for (int k = 0; k < 64; k++) {
                    int p = (a_q6 * (yu[k] - 128) + 32) >> 6;
                    p += b_center;
                    block[k] = (int16_t)std::clamp((int)cu[k] - p, -128, 127);
                }
            }
        }

        if (y_ref) {
            out.cfl_params.push_back(
                {(float)cfl_alpha_q8 / 256.0f, (float)cfl_beta, cfl_applied ? 1.0f : 0.0f, 0.0f}
            );
        }

        int16_t dct_out[64], zigzag[64];
        DCT::forward(block, dct_out);
        Zigzag::scan(dct_out, zigzag);
        std::memcpy(dct_blocks[(size_t)i].data(), zigzag, 128);

        if (aq) {
            float act = QuantTable::calc_activity(&zigzag[1]);
            activities[(size_t)i] = act;
            total_activity += act;
        }
    }

    float avg_activity = total_activity / std::max(1, nb);
    if (aq) out.q_deltas.reserve((size_t)nb);

    int16_t prev_dc = 0;
    for (int i = 0; i < nb; i++) {
        if (i < (int)out.block_types.size() &&
            (out.block_types[(size_t)i] == FileHeader::BlockType::PALETTE ||
             out.block_types[(size_t)i] == FileHeader::BlockType::COPY)) {
            continue;
        }

        float scale = 1.0f;
        if (aq) {
            scale = QuantTable::get_adaptive_scale(activities[(size_t)i], avg_activity);
            int8_t delta =
                (int8_t)std::clamp((scale - 1.0f) * 50.0f, -127.0f, 127.0f);
            out.q_deltas.push_back(delta);
            scale = 1.0f + (delta / 50.0f);
        }

        int16_t quantized[64];
        for (int k = 0; k < 64; k++) {
            int16_t coeff = dct_blocks[(size_t)i][(size_t)k];
            uint16_t q_adj =
                std::max((uint16_t)1, (uint16_t)std::round(quant[k] * scale));
            int sign = (coeff < 0) ? -1 : 1;
            quantized[k] = sign * ((std::abs(coeff) + q_adj / 2) / q_adj);
        }

        int16_t dc_diff = quantized[0] - prev_dc;
        prev_dc = quantized[0];
        out.dc_tokens.push_back(Tokenizer::tokenize_dc(dc_diff));

        if (use_band_group_cdf) {
            tokenize_ac_band(quantized, BAND_LOW, out.ac_low_tokens);
            tokenize_ac_band(quantized, BAND_MID, out.ac_mid_tokens);
            tokenize_ac_band(quantized, BAND_HIGH, out.ac_high_tokens);
        } else {
            auto at = Tokenizer::tokenize_ac(&quantized[1]);
            out.ac_tokens.insert(out.ac_tokens.end(), at.begin(), at.end());
        }
    }

    return out;
}

} // namespace hakonyans::lossy_plane_analysis
