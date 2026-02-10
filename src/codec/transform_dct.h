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
        // Row-wise AAN IDCT
        for (int y = 0; y < 8; y++) idct_1d_aan(&input[y * 8], &temp[y * 8]);
        // Column-wise AAN IDCT
        for (int x = 0; x < 8; x++) {
            int32_t col[8], col_out[8];
            for (int y = 0; y < 8; y++) col[y] = temp[y * 8 + x];
            idct_1d_aan_col(col, col_out);
            for (int y = 0; y < 8; y++) output[y * 8 + x] = (int16_t)col_out[y];
        }
    }

private:
    // AAN/Loeffler fixed-point IDCT constants from libjpeg-turbo jidctint.c.
    static constexpr int CONST_BITS = 13;
    static constexpr int PASS1_BITS = 2;
    static constexpr int32_t FIX_0_298631336 = 2446;
    static constexpr int32_t FIX_0_390180644 = 3196;
    static constexpr int32_t FIX_0_541196100 = 4433;
    static constexpr int32_t FIX_0_765366865 = 6270;
    static constexpr int32_t FIX_0_899976223 = 7373;
    static constexpr int32_t FIX_1_175875602 = 9633;
    static constexpr int32_t FIX_1_501321110 = 12299;
    static constexpr int32_t FIX_1_847759065 = 15137;
    static constexpr int32_t FIX_1_961570560 = 16069;
    static constexpr int32_t FIX_2_053119869 = 16819;
    static constexpr int32_t FIX_2_562915447 = 20995;
    static constexpr int32_t FIX_3_072711026 = 25172;

    static int32_t descale(int32_t value, int bits) {
        return (value + (1 << (bits - 1))) >> bits;
    }

    static int32_t multiply(int32_t value, int32_t constant) {
        return value * constant;
    }

    // First pass: output is scaled by 2^PASS1_BITS.
    static void idct_1d_aan(const int16_t in[8], int32_t out[8]) {
        if (in[1] == 0 && in[2] == 0 && in[3] == 0 && in[4] == 0 &&
            in[5] == 0 && in[6] == 0 && in[7] == 0) {
            const int32_t dcval = static_cast<int32_t>(in[0]) << PASS1_BITS;
            out[0] = dcval;
            out[1] = dcval;
            out[2] = dcval;
            out[3] = dcval;
            out[4] = dcval;
            out[5] = dcval;
            out[6] = dcval;
            out[7] = dcval;
            return;
        }

        int32_t tmp0, tmp1, tmp2, tmp3;
        int32_t tmp10, tmp11, tmp12, tmp13;
        int32_t z1, z2, z3, z4, z5;

        z2 = in[2];
        z3 = in[6];
        z1 = multiply(z2 + z3, FIX_0_541196100);
        tmp2 = z1 + multiply(z3, -FIX_1_847759065);
        tmp3 = z1 + multiply(z2, FIX_0_765366865);

        z2 = in[0];
        z3 = in[4];
        tmp0 = (z2 + z3) * (1 << CONST_BITS);
        tmp1 = (z2 - z3) * (1 << CONST_BITS);

        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        tmp0 = in[7];
        tmp1 = in[5];
        tmp2 = in[3];
        tmp3 = in[1];

        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        z4 = tmp1 + tmp3;
        z5 = multiply(z3 + z4, FIX_1_175875602);

        tmp0 = multiply(tmp0, FIX_0_298631336);
        tmp1 = multiply(tmp1, FIX_2_053119869);
        tmp2 = multiply(tmp2, FIX_3_072711026);
        tmp3 = multiply(tmp3, FIX_1_501321110);
        z1 = multiply(z1, -FIX_0_899976223);
        z2 = multiply(z2, -FIX_2_562915447);
        z3 = multiply(z3, -FIX_1_961570560);
        z4 = multiply(z4, -FIX_0_390180644);

        z3 += z5;
        z4 += z5;

        tmp0 += z1 + z3;
        tmp1 += z2 + z4;
        tmp2 += z2 + z3;
        tmp3 += z1 + z4;

        out[0] = descale(tmp10 + tmp3, CONST_BITS - PASS1_BITS);
        out[7] = descale(tmp10 - tmp3, CONST_BITS - PASS1_BITS);
        out[1] = descale(tmp11 + tmp2, CONST_BITS - PASS1_BITS);
        out[6] = descale(tmp11 - tmp2, CONST_BITS - PASS1_BITS);
        out[2] = descale(tmp12 + tmp1, CONST_BITS - PASS1_BITS);
        out[5] = descale(tmp12 - tmp1, CONST_BITS - PASS1_BITS);
        out[3] = descale(tmp13 + tmp0, CONST_BITS - PASS1_BITS);
        out[4] = descale(tmp13 - tmp0, CONST_BITS - PASS1_BITS);
    }

    // Second pass: undo CONST_BITS + PASS1_BITS scaling and 1/8 factor.
    static void idct_1d_aan_col(const int32_t in[8], int32_t out[8]) {
        if (in[1] == 0 && in[2] == 0 && in[3] == 0 && in[4] == 0 &&
            in[5] == 0 && in[6] == 0 && in[7] == 0) {
            const int32_t dcval = descale(in[0], PASS1_BITS + 3);
            out[0] = dcval;
            out[1] = dcval;
            out[2] = dcval;
            out[3] = dcval;
            out[4] = dcval;
            out[5] = dcval;
            out[6] = dcval;
            out[7] = dcval;
            return;
        }

        int32_t tmp0, tmp1, tmp2, tmp3;
        int32_t tmp10, tmp11, tmp12, tmp13;
        int32_t z1, z2, z3, z4, z5;

        z2 = in[2];
        z3 = in[6];
        z1 = multiply(z2 + z3, FIX_0_541196100);
        tmp2 = z1 + multiply(z3, -FIX_1_847759065);
        tmp3 = z1 + multiply(z2, FIX_0_765366865);

        tmp0 = (in[0] + in[4]) * (1 << CONST_BITS);
        tmp1 = (in[0] - in[4]) * (1 << CONST_BITS);

        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        tmp0 = in[7];
        tmp1 = in[5];
        tmp2 = in[3];
        tmp3 = in[1];

        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        z4 = tmp1 + tmp3;
        z5 = multiply(z3 + z4, FIX_1_175875602);

        tmp0 = multiply(tmp0, FIX_0_298631336);
        tmp1 = multiply(tmp1, FIX_2_053119869);
        tmp2 = multiply(tmp2, FIX_3_072711026);
        tmp3 = multiply(tmp3, FIX_1_501321110);
        z1 = multiply(z1, -FIX_0_899976223);
        z2 = multiply(z2, -FIX_2_562915447);
        z3 = multiply(z3, -FIX_1_961570560);
        z4 = multiply(z4, -FIX_0_390180644);

        z3 += z5;
        z4 += z5;

        tmp0 += z1 + z3;
        tmp1 += z2 + z4;
        tmp2 += z2 + z3;
        tmp3 += z1 + z4;

        constexpr int FINAL_SHIFT = CONST_BITS + PASS1_BITS + 3;
        out[0] = descale(tmp10 + tmp3, FINAL_SHIFT);
        out[7] = descale(tmp10 - tmp3, FINAL_SHIFT);
        out[1] = descale(tmp11 + tmp2, FINAL_SHIFT);
        out[6] = descale(tmp11 - tmp2, FINAL_SHIFT);
        out[2] = descale(tmp12 + tmp1, FINAL_SHIFT);
        out[5] = descale(tmp12 - tmp1, FINAL_SHIFT);
        out[3] = descale(tmp13 + tmp0, FINAL_SHIFT);
        out[4] = descale(tmp13 - tmp0, FINAL_SHIFT);
    }

    // Fixed-point scale for old IDCT coefficients (kept for reference)
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
