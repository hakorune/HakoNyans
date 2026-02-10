#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace hakonyans {

/**
 * 8×8 DCT-II / IDCT with pre-computed fixed-point coefficients
 */
class DCT {
public:
    static void forward(const int16_t input[64], int16_t output[64]) {
        int16_t temp[64];
        for (int y = 0; y < 8; y++) dct_1d(&input[y * 8], &temp[y * 8]);
        for (int x = 0; x < 8; x++) {
            int16_t col[8], col_out[8];
            for (int y = 0; y < 8; y++) col[y] = temp[y * 8 + x];
            dct_1d(col, col_out);
            for (int y = 0; y < 8; y++) output[y * 8 + x] = col_out[y];
        }
    }

    static void inverse(const int16_t input[64], int16_t output[64]) {
        int32_t temp[64];
        // Row-wise IDCT
        for (int y = 0; y < 8; y++) idct_1d_fast(&input[y * 8], &temp[y * 8]);
        // Column-wise IDCT
        for (int x = 0; x < 8; x++) {
            int32_t col[8], col_out[8];
            for (int y = 0; y < 8; y++) col[y] = temp[y * 8 + x];
            idct_1d_fast_col(col, col_out);
            for (int y = 0; y < 8; y++) output[y * 8 + x] = (int16_t)col_out[y];
        }
    }

private:
    // Fixed-point scale for IDCT coefficients
    static constexpr int FP_BITS = 12;
    static constexpr int FP_SCALE = 1 << FP_BITS;
    static constexpr int FP_HALF = 1 << (FP_BITS - 1);

    // Pre-computed IDCT basis: basis[k][n] = round(cos(PI*k*(2n+1)/16) * SCALE)
    // For k=0: multiply by 1/sqrt(2) * 0.5 = 0.3536
    // For k>0: multiply by 0.5
    static constexpr int32_t idct_basis[8][8] = {
        // k=0: C(0,n) * 1/sqrt(2) * 0.5 * FP_SCALE
        { 1448, 1448, 1448, 1448, 1448, 1448, 1448, 1448 },
        // k=1..7: C(k,n) * 0.5 * FP_SCALE
        { 2008, 1702, 1137,  399, -399, -1137, -1702, -2008 },
        { 1892,  784, -784, -1892, -1892, -784,   784,  1892 },
        { 1702, -399, -2008, -1137,  1137,  2008,   399, -1702 },
        { 1448, -1448, -1448,  1448,  1448, -1448, -1448,  1448 },
        { 1137, -2008,   399,  1702, -1702,  -399,  2008, -1137 },
        {  784, -1892,  1892,  -784,  -784,  1892, -1892,   784 },
        {  399, -1137,  1702, -2008,  2008, -1702,  1137,  -399 },
    };

    // Forward DCT basis: basis[k][n] = round(cos(PI*k*(2n+1)/16) * scale)
    static constexpr int32_t dct_basis[8][8] = {
        // k=0: scale = 1/sqrt(2) = 0.707
        { 2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896 },
        // k>0: scale = 1.0
        { 4017, 3405, 2276,  799, -799, -2276, -3405, -4017 },
        { 3784, 1567, -1567, -3784, -3784, -1567,  1567,  3784 },
        { 3405, -799, -4017, -2276,  2276,  4017,   799, -3405 },
        { 2896, -2896, -2896,  2896,  2896, -2896, -2896,  2896 },
        { 2276, -4017,   799,  3405, -3405,  -799,  4017, -2276 },
        { 1567, -3784,  3784, -1567, -1567,  3784, -3784,  1567 },
        {  799, -2276,  3405, -4017,  4017, -3405,  2276,  -799 },
    };

    static void dct_1d(const int16_t input[8], int16_t output[8]) {
        for (int k = 0; k < 8; k++) {
            int32_t sum = 0;
            for (int n = 0; n < 8; n++) sum += input[n] * dct_basis[k][n];
            output[k] = (int16_t)((sum + (1 << 12)) >> 13);
        }
    }

    // Row-wise IDCT: input is int16 quantized coefficients, output is int32 intermediate
    static void idct_1d_fast(const int16_t input[8], int32_t output[8]) {
        for (int n = 0; n < 8; n++) {
            int32_t sum = 0;
            for (int k = 0; k < 8; k++) sum += input[k] * idct_basis[k][n];
            output[n] = sum;  // Keep full precision for column pass
        }
    }

    // Column-wise IDCT: input is int32 intermediate, output is final int16
    static void idct_1d_fast_col(const int32_t input[8], int32_t output[8]) {
        for (int n = 0; n < 8; n++) {
            int64_t sum = 0;
            for (int k = 0; k < 8; k++) sum += (int64_t)input[k] * idct_basis[k][n];
            // Two passes of FP_BITS scaling + rounding
            output[n] = (int32_t)((sum + ((int64_t)1 << (2 * FP_BITS - 1))) >> (2 * FP_BITS));
        }
    }
};

} // namespace hakonyans
