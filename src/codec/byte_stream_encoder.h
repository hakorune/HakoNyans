#pragma once

#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "shared_cdf.h"
#include "headers.h"
#include <vector>

namespace hakonyans::byte_stream_encoder {

/**
 * Encode a byte stream using rANS with data-adaptive CDF.
 * Builds a frequency table from the input data (Laplace smoothing).
 *
 * Format: [4B cdf_size][cdf_data][4B count][4B rans_size][rans_data]
 */
inline std::vector<uint8_t> encode_byte_stream(const std::vector<uint8_t>& bytes) {
    // Build frequency table (alphabet = 256)
    std::vector<uint32_t> freq(256, 1);  // Laplace smoothing
    for (uint8_t b : bytes) freq[b]++;

    CDFTable cdf = CDFBuilder().build_from_freq(freq);

    // Serialize CDF
    std::vector<uint8_t> cdf_data(256 * 4);
    for (int i = 0; i < 256; i++) {
        uint32_t f = cdf.freq[i];
        std::memcpy(&cdf_data[i * 4], &f, 4);
    }

    // Encode symbols
    FlatInterleavedEncoder encoder;
    for (uint8_t b : bytes) {
        encoder.encode_symbol(cdf, b);
    }
    auto rans_bytes = encoder.finish();

    // Pack: cdf_size + cdf + count + rans_size + rans
    std::vector<uint8_t> output;
    uint32_t cdf_size = (uint32_t)cdf_data.size();
    uint32_t count = (uint32_t)bytes.size();
    uint32_t rans_size = (uint32_t)rans_bytes.size();

    output.resize(4);
    std::memcpy(output.data(), &cdf_size, 4);
    output.insert(output.end(), cdf_data.begin(), cdf_data.end());

    size_t off = output.size();
    output.resize(off + 4);
    std::memcpy(&output[off], &count, 4);

    off = output.size();
    output.resize(off + 4);
    std::memcpy(&output[off], &rans_size, 4);

    output.insert(output.end(), rans_bytes.begin(), rans_bytes.end());
    CDFBuilder::cleanup(cdf);
    return output;
}

namespace {

// Private CDF builder for Mode5 shared LZ model
inline const CDFTable& get_mode5_shared_lz_cdf() {
    static const CDFTable cdf = CDFBuilder().build_from_freq(hakonyans::mode5_shared_lz_freq());
    return cdf;
}

} // anonymous namespace

/**
 * Shared/static-CDF variant for Mode5 payload (TileLZ bytes).
 * Uses a pre-built frequency table for LZ-compressed data.
 *
 * Format: [4B count][4B rans_size][rans_data]
 */
inline std::vector<uint8_t> encode_byte_stream_shared_lz(const std::vector<uint8_t>& bytes) {
    const CDFTable& cdf = get_mode5_shared_lz_cdf();

    FlatInterleavedEncoder encoder;
    for (uint8_t b : bytes) {
        encoder.encode_symbol(cdf, b);
    }
    auto rans_bytes = encoder.finish();

    std::vector<uint8_t> output;
    uint32_t count = (uint32_t)bytes.size();
    uint32_t rans_size = (uint32_t)rans_bytes.size();
    output.resize(8);
    std::memcpy(output.data(), &count, 4);
    std::memcpy(output.data() + 4, &rans_size, 4);
    output.insert(output.end(), rans_bytes.begin(), rans_bytes.end());
    return output;
}

} // namespace hakonyans::byte_stream_encoder
