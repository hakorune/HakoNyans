#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace hakonyans {

/**
 * 量子化テーブル + quality スケール
 * 
 * JPEG-like な設計:
 * - 8×8 位置依存 quant matrix
 * - quality 1..100 でスケール
 * - デコーダは deq[k] を掛けるだけ（単純乗算）
 */
class QuantTable {
public:
    /**
     * Base quantization matrix (luminance, quality=50)
     * Based on JPEG Annex K
     */
    static constexpr uint16_t base_quant_luma[64] = {
        16,  11,  10,  16,  24,  40,  51,  61,
        12,  12,  14,  19,  26,  58,  60,  55,
        14,  13,  16,  24,  40,  57,  69,  56,
        14,  17,  22,  29,  51,  87,  80,  62,
        18,  22,  37,  56,  68, 109, 103,  77,
        24,  35,  55,  64,  81, 104, 113,  92,
        49,  64,  78,  87, 103, 121, 120, 101,
        72,  92,  95,  98, 112, 100, 103,  99
    };

    /**
     * Base quantization matrix (chrominance, quality=50)
     * Based on JPEG Annex K
     */
    static constexpr uint16_t base_quant_chroma[64] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99
    };

private:
    static void build_quant_table_internal(int quality, const uint16_t base[64], uint16_t quant[64]) {
        quality = std::clamp(quality, 1, 100);

        // JPEG quality scaling
        float scale;
        if (quality < 50) {
            scale = 5000.0f / quality;
        } else {
            scale = 200.0f - quality * 2.0f;
        }

        for (int i = 0; i < 64; i++) {
            int q = static_cast<int>((base[i] * scale + 50.0f) / 100.0f);
            quant[i] = std::clamp(q, 1, 255);
        }
    }

public:

    /**
     * Build quantization table for given quality
     * @param quality 1..100 (1=worst, 100=best)
     * @param quant Output quantization table (zigzag order)
     */
    static void build_quant_table(int quality, uint16_t quant[64]) {
        build_quant_table_internal(quality, base_quant_luma, quant);
    }

    /**
     * Build quantization table with optional chroma matrix
     * @param quality 1..100 (1=worst, 100=best)
     * @param quant Output quantization table (zigzag order)
     * @param chroma true=use chroma base matrix, false=luma
     */
    static void build_quant_table(int quality, uint16_t quant[64], bool chroma) {
        build_quant_table_internal(quality, chroma ? base_quant_chroma : base_quant_luma, quant);
    }

    /**
     * Build luma + chroma quantization tables
     */
    static void build_quant_tables(int quality_luma, int quality_chroma, uint16_t quant_y[64], uint16_t quant_c[64]) {
        build_quant_table_internal(quality_luma, base_quant_luma, quant_y);
        build_quant_table_internal(quality_chroma, base_quant_chroma, quant_c);
    }

    /**
     * Build dequantization table (for decoder)
     * @param quality 1..100
     * @param deq Output dequantization table (zigzag order)
     */
    static void build_dequant_table(int quality, uint16_t deq[64]) {
        build_quant_table(quality, deq);  // Same values
    }

    /**
     * Quantize 8×8 block (zigzag order)
     * @param coeffs Input DCT coefficients
     * @param quant Quantization table (zigzag order)
     * @param output Output quantized coefficients
     */
    static void quantize(const int16_t coeffs[64], const uint16_t quant[64], int16_t output[64]) {
        for (int i = 0; i < 64; i++) {
            // Round to nearest
            int sign = (coeffs[i] < 0) ? -1 : 1;
            int abs_val = std::abs(coeffs[i]);
            output[i] = sign * ((abs_val + quant[i] / 2) / quant[i]);
        }
    }

    /**
     * Dequantize 8×8 block (zigzag order)
     * @param quantized Input quantized coefficients
     * @param deq Dequantization table (zigzag order)
     * @param output Output reconstructed coefficients
     */
    static void dequantize(const int16_t quantized[64], const uint16_t deq[64], int16_t output[64]) {
        for (int i = 0; i < 64; i++) {
            output[i] = quantized[i] * deq[i];
        }
    }

    /**
     * Calculate block activity based on AC coefficients
     */
    static inline float calc_activity(const int16_t ac_coeffs[63]) {
        float activity = 0.0f;
        for (int i = 0; i < 63; i++) {
            activity += std::abs(ac_coeffs[i]);
        }
        return activity;
    }

    /**
     * Get adaptive quantization scale
     */
    static inline float get_adaptive_scale(
        float activity,
        float avg_activity,
        float base_scale = 1.0f,
        float mask_strength = 0.5f
    ) {
        if (avg_activity < 1e-6) return base_scale;
        float ratio = activity / avg_activity;
        return base_scale * std::pow(ratio, mask_strength);
    }
};

} // namespace hakonyans
