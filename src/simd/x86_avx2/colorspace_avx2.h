#pragma once

#include <immintrin.h>
#include <cstdint>

namespace hakonyans {
namespace simd {
namespace avx2 {

// YCbCr -> RGB (8 pixels)
static inline void ycbcr_to_rgb_row_avx2(
    const uint8_t* y_ptr, const uint8_t* cb_ptr, const uint8_t* cr_ptr,
    uint8_t* rgb_ptr, int width
) {
    // R = Y + 1.402 * (Cr - 128)
    // G = Y - 0.34414 * (Cb - 128) - 0.71414 * (Cr - 128)
    // B = Y + 1.772 * (Cb - 128)
    
    // Fixed point: x128 (shift 7) to avoid int16 overflow
    // Max product: 180 * 127 = 22860 (fits int16_t)
    // R = Y + ((180 * (Cr - 128)) >> 7)  ≈ Y + 1.406*(Cr-128)
    // G = Y - ((44 * (Cb - 128) + 92 * (Cr - 128)) >> 7)
    // B = Y + ((227 * (Cb - 128)) >> 7)  ≈ Y + 1.773*(Cb-128)

    __m256i offset128 = _mm256_set1_epi16(128);
    __m256i coeff_r_cr = _mm256_set1_epi16(180);   // 1.402 * 128 ≈ 180
    __m256i coeff_g_cb = _mm256_set1_epi16(44);    // 0.344 * 128 ≈ 44
    __m256i coeff_g_cr = _mm256_set1_epi16(92);    // 0.714 * 128 ≈ 92
    __m256i coeff_b_cb = _mm256_set1_epi16(227);   // 1.772 * 128 ≈ 227
    
    for (int x = 0; x < width; x += 16) {
        if (x + 16 > width) {
            for (int i = x; i < width; i++) {
                int y = y_ptr[i];
                int cb = cb_ptr[i] - 128;
                int cr = cr_ptr[i] - 128;
                int r = y + ((359 * cr) >> 8);
                int g = y - ((88 * cb + 183 * cr) >> 8);
                int b = y + ((454 * cb) >> 8);
                rgb_ptr[i*3+0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
                rgb_ptr[i*3+1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
                rgb_ptr[i*3+2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
            }
            break;
        }

        __m128i y_128 = _mm_loadu_si128((__m128i*)(y_ptr + x));
        __m128i cb_128 = _mm_loadu_si128((__m128i*)(cb_ptr + x));
        __m128i cr_128 = _mm_loadu_si128((__m128i*)(cr_ptr + x));
        
        __m256i y_256 = _mm256_cvtepu8_epi16(y_128);
        __m256i cb_256 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(cb_128), offset128);
        __m256i cr_256 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(cr_128), offset128);
        
        __m256i r = _mm256_add_epi16(y_256, _mm256_srai_epi16(_mm256_mullo_epi16(cr_256, coeff_r_cr), 7));
        __m256i b = _mm256_add_epi16(y_256, _mm256_srai_epi16(_mm256_mullo_epi16(cb_256, coeff_b_cb), 7));
        __m256i g = _mm256_sub_epi16(y_256, _mm256_srai_epi16(
            _mm256_add_epi16(_mm256_mullo_epi16(cb_256, coeff_g_cb), _mm256_mullo_epi16(cr_256, coeff_g_cr)), 7));
        
        // Store with clamp via scalar (interleaved RGB output)
        int16_t r_buf[16], g_buf[16], b_buf[16];
        _mm256_storeu_si256((__m256i*)r_buf, r);
        _mm256_storeu_si256((__m256i*)g_buf, g);
        _mm256_storeu_si256((__m256i*)b_buf, b);
        
        for (int k = 0; k < 16; k++) {
            int idx = x + k;
            rgb_ptr[idx*3+0] = (uint8_t)std::clamp(r_buf[k], (int16_t)0, (int16_t)255);
            rgb_ptr[idx*3+1] = (uint8_t)std::clamp(g_buf[k], (int16_t)0, (int16_t)255);
            rgb_ptr[idx*3+2] = (uint8_t)std::clamp(b_buf[k], (int16_t)0, (int16_t)255);
        }
    }
}

} // namespace avx2
} // namespace simd
} // namespace hakonyans
