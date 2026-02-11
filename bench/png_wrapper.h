#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <chrono>
#include <cstring>

// Include libpng
#include <png.h>

namespace hakonyans {

/**
 * Exception class for PNG operations
 */
struct PNGError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

/**
 * Result of PNG encoding
 */
struct PNGEncodeResult {
    std::vector<uint8_t> png_data;
    double encode_time_ms = 0.0;
};

/**
 * Result of PNG decoding
 */
struct PNGDecodeResult {
    std::vector<uint8_t> rgb_data;
    int width = 0;
    int height = 0;
    double decode_time_ms = 0.0;
};

namespace detail {

/**
 * Memory write callback for libpng
 */
void png_write_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto* vec = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(png_ptr));
    vec->insert(vec->end(), data, data + length);
}

/**
 * Memory read callback for libpng
 */
void png_read_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto* state = static_cast<std::pair<const uint8_t*, size_t>*>(png_get_io_ptr(png_ptr));
    if (length > state->second) {
        png_error(png_ptr, "Read beyond end of PNG data");
    }
    std::memcpy(data, state->first, length);
    state->first += length;
    state->second -= length;
}

} // namespace detail

/**
 * Encode RGB data to PNG (in-memory) with maximum compression.
 *
 * @param rgb_data RGB interleaved data (R, G, B, R, G, B, ...)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return PNGEncodeResult containing encoded PNG data and timing
 * @throws PNGError if encoding fails
 */
inline PNGEncodeResult encode_png(const uint8_t* rgb_data, int width, int height) {
    PNGEncodeResult result;

    auto start = std::chrono::steady_clock::now();

    // Create PNG write struct
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        throw PNGError("Failed to create PNG write struct");
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        throw PNGError("Failed to create PNG info struct");
    }

    // Set up error handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        throw PNGError("PNG encoding failed");
    }

    // Set compression to maximum
    png_set_compression_level(png_ptr, 9);  // Z_BEST_COMPRESSION
    png_set_filter(png_ptr, 0, PNG_ALL_FILTERS);

    // Set image info
    png_set_IHDR(png_ptr, info_ptr, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    // Set up write callback (write to memory)
    png_set_write_fn(png_ptr, &result.png_data, detail::png_write_callback, nullptr);

    // Write info
    png_write_info(png_ptr, info_ptr);

    // Write row pointers
    std::vector<png_bytep> row_pointers(height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = const_cast<png_bytep>(rgb_data + y * width * 3);
    }
    png_write_image(png_ptr, row_pointers.data());

    // Finish writing
    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    auto end = std::chrono::steady_clock::now();
    result.encode_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

/**
 * Decode PNG data to RGB (in-memory).
 *
 * @param png_data PNG encoded data
 * @param png_size Size of PNG data in bytes
 * @return PNGDecodeResult containing decoded RGB data and timing
 * @throws PNGError if decoding fails
 */
inline PNGDecodeResult decode_png(const uint8_t* png_data, size_t png_size) {
    PNGDecodeResult result;

    auto start = std::chrono::steady_clock::now();

    // Create PNG read struct
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        throw PNGError("Failed to create PNG read struct");
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        throw PNGError("Failed to create PNG info struct");
    }

    // Set up error handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        throw PNGError("PNG decoding failed");
    }

    // Set up read state
    std::pair<const uint8_t*, size_t> read_state(png_data, png_size);
    png_set_read_fn(png_ptr, &read_state, detail::png_read_callback);

    // Read PNG info
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width, height;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
                 nullptr, nullptr, nullptr);

    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);

    // Set up transformations for RGB output
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    // Always RGB output
    png_set_bgr(png_ptr);  // Convert BGR to RGB if needed

    // Update info after transformations
    png_read_update_info(png_ptr, info_ptr);

    // Allocate output buffer
    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    result.rgb_data.resize(row_bytes * height);

    // Read image data
    std::vector<png_bytep> row_pointers(height);
    for (png_uint_32 y = 0; y < height; y++) {
        row_pointers[y] = result.rgb_data.data() + y * row_bytes;
    }
    png_read_image(png_ptr, row_pointers.data());

    // Clean up
    png_read_end(png_ptr, nullptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

    auto end = std::chrono::steady_clock::now();
    result.decode_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

/**
 * Load PNG from file path (utility function).
 *
 * @param filepath Path to PNG file
 * @return PNGDecodeResult containing decoded RGB data
 */
inline PNGDecodeResult load_png_file(const std::string& filepath) {
    // Read file into memory
    std::ifstream f(filepath, std::ios::binary | std::ios::ate);
    if (!f) {
        throw PNGError("Cannot open PNG file: " + filepath);
    }

    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!f.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw PNGError("Failed to read PNG file: " + filepath);
    }

    return decode_png(buffer.data(), buffer.size());
}

} // namespace hakonyans
