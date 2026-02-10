#pragma once

#include "../codec/colorspace.h"
#include "x86_avx2/colorspace_avx2.h"

namespace hakonyans {
namespace simd {

static inline void ycbcr_to_rgb_row(const uint8_t* y, const uint8_t* cb, const uint8_t* cr, uint8_t* rgb, int w) {
#if defined(__AVX2__)
    avx2::ycbcr_to_rgb_row_avx2(y, cb, cr, rgb, w);
#else
    for (int x = 0; x < w; x++) {
        hakonyans::ycbcr_to_rgb(y[x], cb[x], cr[x], rgb[x*3], rgb[x*3+1], rgb[x*3+2]);
    }
#endif
}

}
}