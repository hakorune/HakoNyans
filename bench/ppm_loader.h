#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <fstream>

namespace hakonyans {

/**
 * PPM Image data structure
 */
struct PPMImage {
    int width = 0;
    int height = 0;
    int max_val = 0;
    std::vector<uint8_t> rgb_data;  // RGB interleaved (R, G, B, R, G, B, ...)

    size_t pixel_count() const { return static_cast<size_t>(width) * height; }
    size_t data_size() const { return pixel_count() * 3; }
};

/**
 * Exception class for PPM loading errors
 */
struct PPMLoadError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

namespace detail {

/**
 * Skip comments and whitespace in PPM header
 */
inline void skip_ppm_comments(std::ifstream& f) {
    char c;
    while (f.get(c)) {
        if (isspace(static_cast<unsigned char>(c))) continue;
        if (c == '#') {
            // Skip to end of line
            while (f.get(c) && c != '\n') continue;
            continue;
        }
        f.unget();
        break;
    }
}

/**
 * Parse a single integer value from PPM header
 */
inline int parse_ppm_int(std::ifstream& f) {
    skip_ppm_comments(f);
    int value;
    if (!(f >> value)) {
        throw PPMLoadError("Failed to read integer value from PPM header");
    }
    return value;
}

} // namespace detail

/**
 * Check if a file is a P6 PPM format
 */
inline bool is_ppm_p6(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f) return false;

    std::string magic;
    f >> magic;
    return magic == "P6";
}

/**
 * Load a P6 PPM file from disk.
 *
 * @param filepath Path to the PPM file
 * @return PPMImage struct containing the image data
 * @throws PPMLoadError if the file cannot be loaded or is invalid
 */
inline PPMImage load_ppm(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f) {
        throw PPMLoadError("Cannot open file: " + filepath);
    }

    // Read magic number
    std::string magic;
    f >> magic;
    if (magic != "P6") {
        throw PPMLoadError("Not a P6 PPM file: " + filepath);
    }

    // Read dimensions
    int width = detail::parse_ppm_int(f);
    int height = detail::parse_ppm_int(f);
    int max_val = detail::parse_ppm_int(f);

    if (width <= 0 || height <= 0) {
        throw PPMLoadError("Invalid dimensions: " + std::to_string(width) + "x" + std::to_string(height));
    }
    if (max_val != 255) {
        throw PPMLoadError("Only 8-bit PPM (max_val=255) is supported, got: " + std::to_string(max_val));
    }

    // Skip single whitespace after max_val
    char ws;
    f.get(ws);
    if (!isspace(static_cast<unsigned char>(ws))) {
        throw PPMLoadError("Expected whitespace after max_val");
    }

    // Read binary RGB data
    size_t data_size = static_cast<size_t>(width) * height * 3;
    std::vector<uint8_t> rgb_data(data_size);

    f.read(reinterpret_cast<char*>(rgb_data.data()), data_size);
    if (!f) {
        throw PPMLoadError("Failed to read pixel data from: " + filepath);
    }

    PPMImage result;
    result.width = width;
    result.height = height;
    result.max_val = max_val;
    result.rgb_data = std::move(rgb_data);

    return result;
}

/**
 * Save a PPM image to disk (utility function for testing)
 */
inline void save_ppm(const std::string& filepath, const uint8_t* rgb_data, int width, int height) {
    std::ofstream f(filepath, std::ios::binary);
    if (!f) {
        throw PPMLoadError("Cannot create file: " + filepath);
    }

    f << "P6\n" << width << " " << height << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb_data), static_cast<size_t>(width) * height * 3);

    if (!f) {
        throw PPMLoadError("Failed to write PPM data to: " + filepath);
    }
}

} // namespace hakonyans
