#pragma once

#include <cstdint>
#include <algorithm>

namespace hakonyans {

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

} // namespace hakonyans
