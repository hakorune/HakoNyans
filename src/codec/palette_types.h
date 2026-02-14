#pragma once

#include <cstdint>
#include <cstring>

namespace hakonyans {

struct Palette {
    uint8_t size;
    int16_t colors[8];

    Palette() : size(0) { std::memset(colors, 0, sizeof(colors)); }

    bool operator==(const Palette& other) const {
        if (size != other.size) return false;
        return std::memcmp(colors, other.colors, size * sizeof(int16_t)) == 0;
    }

    bool operator!=(const Palette& other) const { return !(*this == other); }
};

} // namespace hakonyans
