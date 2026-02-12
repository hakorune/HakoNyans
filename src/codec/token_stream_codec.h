#pragma once

#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/pindex.h"
#include "headers.h"
#include <algorithm>
#include <vector>

namespace hakonyans::token_stream_codec {

/**
 * Build CDF table from token frequency.
 * Uses Laplace smoothing (initial count = 1) for all 76 symbol types.
 */
inline CDFTable build_cdf(const std::vector<Token>& t) {
    std::vector<uint32_t> f(76, 1);
    for (const auto& x : t) {
        int sym = static_cast<int>(x.type);
        if (sym < 76) f[sym]++;
    }
    return CDFBuilder().build_from_freq(f);
}

/**
 * Calculate P-Index checkpoint interval based on target metadata ratio.
 * Ensures interval is 8-aligned and clamped to [64, 4096].
 */
inline int calculate_pindex_interval(
    size_t token_count,
    size_t encoded_token_stream_bytes,
    int target_meta_ratio_percent = 2
) {
    if (token_count == 0 || encoded_token_stream_bytes == 0) return 4096;
    target_meta_ratio_percent = std::clamp(target_meta_ratio_percent, 1, 10);
    double target_meta_bytes = (double)encoded_token_stream_bytes * (double)target_meta_ratio_percent / 100.0;
    // P-Index serialization: 12-byte header + 40 bytes/checkpoint.
    double target_checkpoints = (target_meta_bytes - 12.0) / 40.0;
    if (target_checkpoints < 1.0) target_checkpoints = 1.0;
    double raw_interval = (double)token_count / target_checkpoints;
    int interval = (int)std::llround(raw_interval);
    interval = std::clamp(interval, 64, 4096);
    interval = ((interval + 7) / 8) * 8;  // PIndexBuilder expects 8-aligned token interval.
    return std::clamp(interval, 64, 4096);
}

/**
 * Serialize band-grouped P-Index blob.
 * Packs low/mid/high band P-Index streams into a single blob with size headers.
 */
inline std::vector<uint8_t> serialize_band_pindex_blob(
    const std::vector<uint8_t>& low,
    const std::vector<uint8_t>& mid,
    const std::vector<uint8_t>& high
) {
    if (low.empty() && mid.empty() && high.empty()) return {};
    std::vector<uint8_t> out;
    out.resize(12);
    uint32_t low_sz = (uint32_t)low.size();
    uint32_t mid_sz = (uint32_t)mid.size();
    uint32_t high_sz = (uint32_t)high.size();
    std::memcpy(&out[0], &low_sz, 4);
    std::memcpy(&out[4], &mid_sz, 4);
    std::memcpy(&out[8], &high_sz, 4);
    out.insert(out.end(), low.begin(), low.end());
    out.insert(out.end(), mid.begin(), mid.end());
    out.insert(out.end(), high.begin(), high.end());
    return out;
}

/**
 * Encode tokens using rANS with data-adaptive CDF.
 * Optionally generates P-Index for random access decoding.
 *
 * Format: [4B cdf_size][cdf_data][4B count][4B rans_size][rans_data][4B raw_count][raw_data]
 */
inline std::vector<uint8_t> encode_tokens(
    const std::vector<Token>& t,
    const CDFTable& c,
    std::vector<uint8_t>* out_pi = nullptr,
    int target_pindex_meta_ratio_percent = 2,
    size_t min_pindex_stream_bytes = 0
) {
    std::vector<uint8_t> output;
    int alpha = c.alphabet_size;
    std::vector<uint8_t> cdf_data(alpha * 4);
    for (int i = 0; i < alpha; i++) {
        uint32_t f = c.freq[i];
        std::memcpy(&cdf_data[i * 4], &f, 4);
    }
    uint32_t cdf_size = cdf_data.size();
    output.resize(4);
    std::memcpy(output.data(), &cdf_size, 4);
    output.insert(output.end(), cdf_data.begin(), cdf_data.end());

    uint32_t token_count = t.size();
    size_t count_offset = output.size();
    output.resize(count_offset + 4);
    std::memcpy(&output[count_offset], &token_count, 4);

    FlatInterleavedEncoder encoder;
    for (const auto& tok : t) {
        encoder.encode_symbol(c, static_cast<uint8_t>(tok.type));
    }
    auto rb = encoder.finish();

    uint32_t rans_size = rb.size();
    size_t rs_offset = output.size();
    output.resize(rs_offset + 4);
    std::memcpy(&output[rs_offset], &rans_size, 4);
    output.insert(output.end(), rb.begin(), rb.end());

    std::vector<uint8_t> raw_data;
    uint32_t raw_count = 0;
    for (const auto& tok : t) {
        if (tok.raw_bits_count > 0) {
            raw_data.push_back(tok.raw_bits_count);
            raw_data.push_back(tok.raw_bits & 0xFF);
            raw_data.push_back((tok.raw_bits >> 8) & 0xFF);
            raw_count++;
        }
    }
    size_t rc_offset = output.size();
    output.resize(rc_offset + 4);
    std::memcpy(&output[rc_offset], &raw_count, 4);
    output.insert(output.end(), raw_data.begin(), raw_data.end());

    if (out_pi) {
        if (t.empty() || output.size() < min_pindex_stream_bytes) {
            out_pi->clear();
        } else {
            int interval = calculate_pindex_interval(
                t.size(), output.size(), target_pindex_meta_ratio_percent
            );
            auto pindex = PIndexBuilder::build(rb, c, t.size(), (uint32_t)interval);
            *out_pi = PIndexCodec::serialize(pindex);
        }
    }
    return output;
}

} // namespace hakonyans::token_stream_codec
