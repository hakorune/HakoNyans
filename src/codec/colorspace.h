#pragma once

#include <cstdint>
#include <algorithm>
#include <vector>

namespace hakonyans {

/**
 * Chroma subsampling types
 */
enum class ChromaSubsampling {
    CS_444 = 0,
    CS_420 = 1,
};

/**
 * RGB to YCbCr conversion (JPEG-style integer approximation)
 * Y: [0, 255], Cb: [0, 255], Cr: [0, 255]
 */
inline void rgb_to_ycbcr(uint8_t r, uint8_t g, uint8_t b,
                         uint8_t& y, uint8_t& cb, uint8_t& cr) {
    // Standard JPEG conversion (integer math)
    // Y  =  0.299R + 0.587G + 0.114B
    // Cb = -0.1687R - 0.3313G + 0.5B + 128
    // Cr =  0.5R - 0.4187G - 0.0813B + 128
    
    int yy  = ( (77 * r + 150 * g + 29 * b) >> 8);
    int cb_ = ((-43 * r - 85 * g + 128 * b) >> 8) + 128;
    int cr_ = ((128 * r - 107 * g - 21 * b) >> 8) + 128;
    
    y  = static_cast<uint8_t>(std::clamp(yy, 0, 255));
    cb = static_cast<uint8_t>(std::clamp(cb_, 0, 255));
    cr = static_cast<uint8_t>(std::clamp(cr_, 0, 255));
}

/**
 * YCbCr to RGB conversion
 */
inline void ycbcr_to_rgb(uint8_t y, uint8_t cb, uint8_t cr,
                         uint8_t& r, uint8_t& g, uint8_t& b) {
    int cb_ = cb - 128;
    int cr_ = cr - 128;
    
    // R = Y + 1.402 * (Cr - 128)
    // G = Y - 0.34414 * (Cb - 128) - 0.71414 * (Cr - 128)
    // B = Y + 1.772 * (Cb - 128)
    
    int rr = y + ((359 * cr_) >> 8);
    int gg = y - ((88 * cb_ + 183 * cr_) >> 8);
    int bb = y + ((454 * cb_) >> 8);
    
    r = static_cast<uint8_t>(std::clamp(rr, 0, 255));
    g = static_cast<uint8_t>(std::clamp(gg, 0, 255));
    b = static_cast<uint8_t>(std::clamp(bb, 0, 255));
}

/**
 * 4:2:0 Downsampling (2x2 average)
 */
inline void downsample_420(
    const uint8_t* src, int w, int h,
    std::vector<uint8_t>& dst, int& out_w, int& out_h
) {
    out_w = (w + 1) / 2;
    out_h = (h + 1) / 2;
    dst.resize(out_w * out_h);
    
    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            int y0 = std::min(2 * y, h - 1);
            int y1 = std::min(2 * y + 1, h - 1);
            int x0 = std::min(2 * x, w - 1);
            int x1 = std::min(2 * x + 1, w - 1);
            
            uint16_t sum = src[y0 * w + x0] + src[y0 * w + x1] +
                           src[y1 * w + x0] + src[y1 * w + x1];
            dst[y * out_w + x] = static_cast<uint8_t>(sum >> 2);
        }
    }
}

/**
 * CfL (Chroma from Luma) parameters for a block
 */
struct CfLParams {
    float alpha_cb = 0.0f;
    float beta_cb = 0.0f;
    float alpha_cr = 0.0f;
    float beta_cr = 0.0f;
};

/**
 * Compute CfL parameters for a block using integer fixed-point arithmetic.
 * Returns alpha (Q8) and beta (integer).
 */
inline void compute_cfl_block_adaptive(
    const uint8_t* y_block,
    const uint8_t* c_block,
    int& alpha_q8,
    int& beta,
    int count = 64
) {
    int64_t sum_y = 0, sum_c = 0, sum_y2 = 0, sum_yc = 0;
    for (int i = 0; i < count; i++) {
        int y = y_block[i];
        int c = c_block[i];
        sum_y += y;
        sum_c += c;
        sum_y2 += (int64_t)y * y;
        sum_yc += (int64_t)y * c;
    }

    int64_t var_y_x64 = (sum_y2 * count) - (sum_y * sum_y); // variance * count^2
    int64_t cov_yc_x64 = (sum_yc * count) - (sum_y * sum_c); // covariance * count^2

    if (var_y_x64 < 1024) { // Luma variance too low, unreliable for CfL
        alpha_q8 = 0;
        beta = (int)((sum_c + (count >> 1)) / count);
    } else {
        // alpha = cov / var. In Q8: alpha_q8 = (cov * 256) / var
        int64_t a8 = (cov_yc_x64 * 256 + (var_y_x64 >> 1)) / var_y_x64;
        alpha_q8 = (int)std::clamp(a8, (int64_t)-255, (int64_t)255);
        
        // We want: pred = alpha * (y - 128) + beta
        // At y = avg_y, we want pred = avg_c.
        // avg_c = alpha * (avg_y - 128) + beta
        // beta = avg_c - alpha * (avg_y - 128)
        // beta = (sum_c/count) - (alpha_q8/256) * (sum_y/count - 128)
        // beta = (sum_c * 256 - alpha_q8 * (sum_y - count * 128)) / (count * 256)
        beta = (int)((sum_c * 256 - (int64_t)alpha_q8 * (sum_y - count * 128) + (count * 128)) / (count * 256));
        beta = std::clamp(beta, 0, 255);
    }
}

/**
 * YCoCg-R: Reversible (lossless) color transform
 * 
 * RGB → Y, Co, Cg conversion with NO information loss.
 * Co, Cg range: [-255, 255] (9 bits, stored as int16_t)
 * Y range: [0, 255]
 * 
 * Reference: Malvar & Sullivan, "YCoCg-R: A Color Space with RGB Reversibility
 * and Low Dynamic Range" (2003)
 */
inline void rgb_to_ycocg_r(uint8_t r, uint8_t g, uint8_t b,
                            int16_t& y, int16_t& co, int16_t& cg) {
    co = (int16_t)r - (int16_t)b;             // [-255, 255]
    int16_t tmp = (int16_t)b + (co >> 1);      // floor division
    cg = (int16_t)g - tmp;                     // [-255, 255]
    y  = tmp + (cg >> 1);                      // [0, 255]
}

/**
 * YCoCg-R inverse: Y, Co, Cg → RGB
 * Exact inverse of rgb_to_ycocg_r (bit-exact)
 */
inline void ycocg_r_to_rgb(int16_t y, int16_t co, int16_t cg,
                            uint8_t& r, uint8_t& g, uint8_t& b) {
    int16_t tmp = y - (cg >> 1);
    int16_t g16 = tmp + cg;
    int16_t b16 = tmp - (co >> 1);
    int16_t r16 = b16 + co;
    r = (uint8_t)std::clamp((int)r16, 0, 255);
    g = (uint8_t)std::clamp((int)g16, 0, 255);
    b = (uint8_t)std::clamp((int)b16, 0, 255);
}

/**
 * ZigZag encode: signed → unsigned (for entropy coding)
 * Maps: 0→0, -1→1, 1→2, -2→3, 2→4, ...
 */
inline uint16_t zigzag_encode_val(int16_t val) {
    return (uint16_t)((val << 1) ^ (val >> 15));
}

/**
 * ZigZag decode: unsigned → signed
 */
inline int16_t zigzag_decode_val(uint16_t val) {
    return (int16_t)((val >> 1) ^ -(int16_t)(val & 1));
}

} // namespace hakonyans
