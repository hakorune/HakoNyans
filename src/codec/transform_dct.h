#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace hakonyans {

/**
 * 8×8 DCT-II / IDCT
 */
class DCT {
public:
    static void forward(const int16_t input[64], int16_t output[64]) {
        int16_t temp[64];
        for (int y = 0; y < 8; y++) dct_1d(&input[y * 8], &temp[y * 8], 1);
        for (int x = 0; x < 8; x++) {
            int16_t col[8], col_out[8];
            for (int y = 0; y < 8; y++) col[y] = temp[y * 8 + x];
            dct_1d(col, col_out, 1);
            for (int y = 0; y < 8; y++) output[y * 8 + x] = col_out[y];
        }
    }

    static void inverse(const int16_t input[64], int16_t output[64]) {
        // Fallback to float implementation for now to ensure correctness
        // SIMD version will replace this in Step 2
        int16_t temp[64];
        for (int y = 0; y < 8; y++) idct_1d(&input[y * 8], &temp[y * 8], 1);
        for (int x = 0; x < 8; x++) {
            int16_t col[8], col_out[8];
            for (int y = 0; y < 8; y++) col[y] = temp[y * 8 + x];
            idct_1d(col, col_out, 1);
            for (int y = 0; y < 8; y++) output[y * 8 + x] = col_out[y];
        }
    }

private:
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

    static void idct_1d(const int16_t input[8], int16_t output[8], int stride) {
        static const float sqrt2 = 1.41421356f;
        for (int n = 0; n < 8; n++) {
            float sum = input[0] / sqrt2;
            for (int k = 1; k < 8; k++) {
                float angle = M_PI * k * (2 * n + 1) / 16.0f;
                sum += input[k * stride] * std::cos(angle);
            }
            output[n * stride] = static_cast<int16_t>(std::round(sum * 0.5f));
        }
    }
};

} // namespace hakonyans
