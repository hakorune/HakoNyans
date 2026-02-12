#pragma once

#include <cstdint>
#include <vector>

namespace hakonyans {

// Shared/static byte-frequency model for Mode5 (TileLZ payload + rANS).
// This avoids per-tile 256-entry CDF serialization overhead.
inline const std::vector<uint32_t>& mode5_shared_lz_freq() {
    static const std::vector<uint32_t> freq = [] {
        std::vector<uint32_t> f(256, 1);

        // TileLZ stream tags (0=LITRUN, 1=MATCH) are frequent.
        f[0] += 1024;
        f[1] += 768;

        // Small lengths / small values are common in token fields.
        for (int i = 2; i <= 16; i++) f[i] += 192;
        for (int i = 17; i <= 63; i++) f[i] += 64;
        for (int i = 64; i <= 127; i++) f[i] += 24;
        for (int i = 128; i <= 255; i++) f[i] += 8;

        // Additional bias toward very small bytes.
        for (int i = 0; i < 8; i++) f[i] += 128;
        return f;
    }();
    return freq;
}

} // namespace hakonyans

