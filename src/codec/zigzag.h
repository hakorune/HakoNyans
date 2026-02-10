#pragma once

#include <cstdint>

namespace hakonyans {

/**
 * Zigzag scan order for 8×8 blocks
 * 
 * 低周波（DC）→ 高周波へとスキャン。
 * ゼロが末尾に集まりやすい → RLE/EOB と相性良好。
 */
class Zigzag {
public:
    /**
     * Zigzag order (forward scan)
     * zigzag[i] = 2D position in raster order
     */
    static constexpr int forward[64] = {
         0,  1,  8, 16,  9,  2,  3, 10,
        17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };

    /**
     * Inverse zigzag order (scan position → raster)
     * inverse[i] = zigzag position of raster[i]
     */
    static constexpr int inverse[64] = {
         0,  1,  5,  6, 14, 15, 27, 28,
         2,  4,  7, 13, 16, 26, 29, 42,
         3,  8, 12, 17, 25, 30, 41, 43,
         9, 11, 18, 24, 31, 40, 44, 53,
        10, 19, 23, 32, 39, 45, 52, 54,
        20, 22, 33, 38, 46, 51, 55, 60,
        21, 34, 37, 47, 50, 56, 59, 61,
        35, 36, 48, 49, 57, 58, 62, 63
    };

    /**
     * Apply zigzag scan to 8×8 block
     * @param block Input in raster order [y*8+x]
     * @param output Output in zigzag order
     */
    static void scan(const int16_t block[64], int16_t output[64]) {
        for (int i = 0; i < 64; i++) {
            output[i] = block[forward[i]];
        }
    }

    /**
     * Apply inverse zigzag scan to 8×8 block
     * @param zigzag_data Input in zigzag order
     * @param block Output in raster order [y*8+x]
     */
    static void inverse_scan(const int16_t zigzag_data[64], int16_t block[64]) {
        for (int i = 0; i < 64; i++) {
            block[forward[i]] = zigzag_data[i];
        }
    }
};

} // namespace hakonyans
