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
     * Build quantization table for given quality
     * @param quality 1..100 (1=worst, 100=best)
     * @param quant Output quantization table (zigzag order)
     */
    static void build_quant_table(int quality, uint16_t quant[64]) {
        quality = std::clamp(quality, 1, 100);
        
        // JPEG quality scaling
        float scale;
        if (quality < 50) {
            scale = 5000.0f / quality;
        } else {
            scale = 200.0f - quality * 2.0f;
        }
        
        for (int i = 0; i < 64; i++) {
            int q = static_cast<int>((base_quant_luma[i] * scale + 50.0f) / 100.0f);
            quant[i] = std::clamp(q, 1, 255);
        }
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
