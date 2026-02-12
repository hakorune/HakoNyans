#pragma once

#include "headers.h"
#include <vector>

namespace hakonyans::lossy_image_helpers {

/**
 * Pad image to multiple of 8x8 block boundary.
 * Replicates edge pixels for padding.
 */
inline std::vector<uint8_t> pad_image(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h) {
    std::vector<uint8_t> padded(pad_w * pad_h);
    for (uint32_t y = 0; y < pad_h; y++) {
        for (uint32_t x = 0; x < pad_w; x++) {
            padded[y * pad_w + x] = pixels[std::min(y, height - 1) * width + std::min(x, width - 1)];
        }
    }
    return padded;
}

/**
 * Extract 8x8 block from image.
 * Converts uint8_t pixels to int16_t block with level shift (-128).
 */
inline void extract_block(const uint8_t* pixels, uint32_t stride, uint32_t height, int bx, int by, int16_t block[64]) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            block[y * 8 + x] = static_cast<int16_t>(pixels[(by * 8 + y) * stride + (bx * 8 + x)]) - 128;
        }
    }
}

} // namespace hakonyans::lossy_image_helpers
