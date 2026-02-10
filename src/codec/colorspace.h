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
 * Compute CfL parameters for a block using Least Squares
 */
inline CfLParams compute_cfl_params(
    const uint8_t* y_block,
    const uint8_t* cb_block,
    const uint8_t* cr_block,
    int count = 64
) {
    auto compute_single = [&](const uint8_t* c_block, float& alpha, float& beta) {
        float sum_y = 0, sum_c = 0, sum_y2 = 0, sum_yc = 0;
        for (int i = 0; i < count; i++) {
            float y = y_block[i];
            float c = c_block[i];
            sum_y += y;
            sum_c += c;
            sum_y2 += y * y;
            sum_yc += y * c;
        }
        float avg_y = sum_y / count;
        float avg_c = sum_c / count;
        float var_y = (sum_y2 / count) - (avg_y * avg_y);
        float cov_yc = (sum_yc / count) - (avg_y * avg_c);
        alpha = (std::abs(var_y) > 1e-6f) ? (cov_yc / var_y) : 0.0f;
        beta = avg_c - alpha * avg_y;
    };

    CfLParams params;
    compute_single(cb_block, params.alpha_cb, params.beta_cb);
    compute_single(cr_block, params.alpha_cr, params.beta_cr);
    return params;
}

} // namespace hakonyans
