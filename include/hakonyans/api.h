#pragma once

#include <cstdint>
#include <cstddef>

namespace hakonyans {

constexpr uint32_t MAGIC = 0x484B4E00;  // "HKN\0"
constexpr uint16_t VERSION = 0x0001;

enum class ColorSpace : uint8_t {
    YCbCr = 0,
    RGB = 1,
    Grayscale = 2,
};

enum class TransformType : uint8_t {
    DCT = 0,
};

enum class EntropyType : uint8_t {
    NyANS_P = 0,
};

struct ImageInfo {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    ColorSpace colorspace;
};

// Forward declarations
enum class Status {
    OK = 0,
    ERROR_INVALID_HEADER,
    ERROR_CORRUPT_STREAM,
    ERROR_UNSUPPORTED,
    ERROR_OUT_OF_MEMORY,
};

}  // namespace hakonyans
