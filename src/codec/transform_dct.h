#pragma once

#include <cstdint>
#include <cmath>

namespace hakonyans {

/**
 * 8×8 DCT-II / IDCT (分離可能 1D×2)
 * 
 * Phase 5: スカラー実装（正確性優先）
 * Phase 5.1: AVX2 SIMD 実装に置き換え
 * 
 * AAN (Arai-Agui-Nakajima) スケーリング統合は Phase 5.1 で実装。
 */
class DCT {
public:
    /**
     * Forward 8×8 DCT-II
     * @param input  Input pixels (raster order, [y*8+x])
     * @param output Output DCT coefficients (raster order)
     */
    static void forward(const int16_t input[64], int16_t output[64]) {
        int16_t temp[64];
        
        // Row transform
        for (int y = 0; y < 8; y++) {
            dct_1d(&input[y * 8], &temp[y * 8], 1);
        }
        
        // Column transform (transpose access)
        for (int x = 0; x < 8; x++) {
            int16_t col[8];
            for (int y = 0; y < 8; y++) {
                col[y] = temp[y * 8 + x];
            }
            
            int16_t col_out[8];
            dct_1d(col, col_out, 1);
            
            for (int y = 0; y < 8; y++) {
                output[y * 8 + x] = col_out[y];
            }
        }
    }

    /**
     * Inverse 8×8 DCT-II
     * @param input  Input DCT coefficients (raster order)
     * @param output Output reconstructed pixels (raster order)
     */
    static void inverse(const int16_t input[64], int16_t output[64]) {
        int16_t temp[64];
        
        // Row transform
        for (int y = 0; y < 8; y++) {
            idct_1d(&input[y * 8], &temp[y * 8], 1);
        }
        
        // Column transform (transpose access)
        for (int x = 0; x < 8; x++) {
            int16_t col[8];
            for (int y = 0; y < 8; y++) {
                col[y] = temp[y * 8 + x];
            }
            
            int16_t col_out[8];
            idct_1d(col, col_out, 1);
            
            for (int y = 0; y < 8; y++) {
                output[y * 8 + x] = col_out[y];
            }
        }
    }

private:
    /**
     * 1D DCT-II (8-point)
     * F[k] = scale * Σ f[n] * cos(π*k*(2n+1)/16)
     */
    static void dct_1d(const int16_t input[8], int16_t output[8], int stride) {
        static const float sqrt2 = 1.41421356f;
        
        for (int k = 0; k < 8; k++) {
            float sum = 0.0f;
            for (int n = 0; n < 8; n++) {
                float angle = M_PI * k * (2 * n + 1) / 16.0f;
                sum += input[n * stride] * std::cos(angle);
            }
            
            float scale = (k == 0) ? (1.0f / sqrt2) : 1.0f;
            output[k * stride] = static_cast<int16_t>(sum * scale * 0.5f + 0.5f);
        }
    }

    /**
     * 1D IDCT-II (8-point)
     * f[n] = Σ scale[k] * F[k] * cos(π*k*(2n+1)/16)
     */
    static void idct_1d(const int16_t input[8], int16_t output[8], int stride) {
        static const float sqrt2 = 1.41421356f;
        
        for (int n = 0; n < 8; n++) {
            float sum = input[0] / sqrt2;
            for (int k = 1; k < 8; k++) {
                float angle = M_PI * k * (2 * n + 1) / 16.0f;
                sum += input[k * stride] * std::cos(angle);
            }
            
            output[n * stride] = static_cast<int16_t>(sum * 0.5f + 0.5f);
        }
    }
};

} // namespace hakonyans
