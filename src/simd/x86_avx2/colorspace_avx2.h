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
    
    // Fixed point: x256 (shift 8)
    // R = Y + ((359 * (Cr - 128)) >> 8)
    // G = Y - ((88 * (Cb - 128) + 183 * (Cr - 128)) >> 8)
    // B = Y + ((454 * (Cb - 128)) >> 8)

    __m256i offset128 = _mm256_set1_epi16(128);
    __m256i coeff_r_cr = _mm256_set1_epi16(359);
    __m256i coeff_g_cb = _mm256_set1_epi16(88);
    __m256i coeff_g_cr = _mm256_set1_epi16(183);
    __m256i coeff_b_cb = _mm256_set1_epi16(454);
    
    for (int x = 0; x < width; x += 16) {
        if (x + 16 > width) {
            // Fallback for last few pixels
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

        // Load 16 pixels
        __m128i y_128 = _mm_loadu_si128((__m128i*)(y_ptr + x));
        __m128i cb_128 = _mm_loadu_si128((__m128i*)(cb_ptr + x));
        __m128i cr_128 = _mm_loadu_si128((__m128i*)(cr_ptr + x));
        
        // Convert to 16-bit (0..255)
        __m256i y_256 = _mm256_cvtepu8_epi16(y_128);
        __m256i cb_256 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(cb_128), offset128);
        __m256i cr_256 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(cr_128), offset128);
        
        // R
        __m256i r_term = _mm256_srai_epi16(_mm256_mullo_epi16(cr_256, coeff_r_cr), 8);
        __m256i r = _mm256_add_epi16(y_256, r_term);
        
        // B
        __m256i b_term = _mm256_srai_epi16(_mm256_mullo_epi16(cb_256, coeff_b_cb), 8);
        __m256i b = _mm256_add_epi16(y_256, b_term);
        
        // G
        __m256i g_term1 = _mm256_mullo_epi16(cb_256, coeff_g_cb);
        __m256i g_term2 = _mm256_mullo_epi16(cr_256, coeff_g_cr);
        __m256i g_term = _mm256_srai_epi16(_mm256_add_epi16(g_term1, g_term2), 8);
        __m256i g = _mm256_sub_epi16(y_256, g_term);
        
        // Pack back to 8-bit (using packus for saturation 0..255)
        // We have 16 pixels (256-bit ymm), need to split to low/high for packing?
        // _mm256_packus_epi16 takes two 256-bit registers (32 words) and outputs one 256-bit (32 bytes)
        // But we have R, G, B in separate registers.
        // We need to interleave them: R G B R G B ...
        
        // Simple pack for now: Store R, G, B separately? No, input expects interleaved RGB.
        // Interleaving is complex in SIMD.
        // Let's store plane-wise first if easy, but decode_color expects interleaved.
        // We need to shuffle.
        
        // Pack R, G, B to 8-bit
        // r, g, b are __m256i (16x int16).
        // To get __m128i (16x uint8), we need a helper.
        // _mm256_packus_epi16 does 16-bit -> 8-bit but mixes lanes.
        
        // Permute to fix packus lane crossing
        __m256i r_pack = _mm256_packus_epi16(r, r); // Low 128: 0..7, 0..7; High 128: 8..15, 8..15
        __m256i g_pack = _mm256_packus_epi16(g, g);
        __m256i b_pack = _mm256_packus_epi16(b, b);
        
        __m256i p = _mm256_permute4x64_epi64(r_pack, 0xD8); // Reorder
        __m128i r8 = _mm256_castsi256_si128(p); // Actually complicated.
        
        // Let's just store element-wise for now or use a simpler scalar loop for storing? 
        // No, that defeats the purpose.
        
        // Store 8 pixels at a time (lower half of ymm)
        // R0 G0 B0 R1 G1 B1 ...
        // We can do it by 3 passes or shuffle.
        
        // Use scalar store for now to verify math speedup? No, store is the bottleneck.
        // Implementation of interleaving:
        
        uint8_t r_buf[32], g_buf[32], b_buf[32];
        _mm256_storeu_si256((__m256i*)r_buf, r); // Wait, r is int16 (32 bytes)
        _mm256_storeu_si256((__m256i*)g_buf, g);
        _mm256_storeu_si256((__m256i*)b_buf, b);
        
        for(int k=0; k<16; k++) {
            int idx = x + k;
            rgb_ptr[idx*3+0] = (uint8_t)std::clamp((int16_t)((int16_t*)r_buf)[k], (int16_t)0, (int16_t)255);
            rgb_ptr[idx*3+1] = (uint8_t)std::clamp((int16_t)((int16_t*)g_buf)[k], (int16_t)0, (int16_t)255);
            rgb_ptr[idx*3+2] = (uint8_t)std::clamp((int16_t)((int16_t*)b_buf)[k], (int16_t)0, (int16_t)255);
        }
        
        // This hybrid approach is faster than full scalar but slower than full SIMD.
        // Good enough for Step 2.
    }
}

} // namespace avx2
} // namespace simd
} // namespace hakonyans
