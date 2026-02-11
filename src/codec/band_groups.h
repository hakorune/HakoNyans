#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdlib>

#include "../entropy/nyans_p/tokenization_v2.h"

namespace hakonyans {

#ifndef HAKONYANS_BAND_LOW_END
#define HAKONYANS_BAND_LOW_END 15
#endif

#ifndef HAKONYANS_BAND_MID_END
#define HAKONYANS_BAND_MID_END 31
#endif

static_assert(HAKONYANS_BAND_LOW_END >= 1, "HAKONYANS_BAND_LOW_END must be >= 1");
static_assert(HAKONYANS_BAND_LOW_END <= 61, "HAKONYANS_BAND_LOW_END must be <= 61");
static_assert(HAKONYANS_BAND_MID_END >= 2, "HAKONYANS_BAND_MID_END must be >= 2");
static_assert(HAKONYANS_BAND_MID_END <= 62, "HAKONYANS_BAND_MID_END must be <= 62");
static_assert(HAKONYANS_BAND_LOW_END < HAKONYANS_BAND_MID_END,
              "HAKONYANS_BAND_LOW_END must be < HAKONYANS_BAND_MID_END");

static constexpr int BAND_LOW_END_ZZ = HAKONYANS_BAND_LOW_END;
static constexpr int BAND_MID_END_ZZ = HAKONYANS_BAND_MID_END;

enum BandGroup : uint8_t {
    BAND_DC = 0,
    BAND_LOW = 1,
    BAND_MID = 2,
    BAND_HIGH = 3
};

constexpr BandGroup band_from_zigzag_index(int zigzag_idx) {
    if (zigzag_idx <= 0) return BAND_DC;
    if (zigzag_idx <= BAND_LOW_END_ZZ) return BAND_LOW;
    if (zigzag_idx <= BAND_MID_END_ZZ) return BAND_MID;
    return BAND_HIGH;
}

// Zigzag coefficient index (0..63) -> band
static constexpr std::array<BandGroup, 64> ZIGZAG_TO_BAND = []() constexpr {
    std::array<BandGroup, 64> lut{};
    for (int i = 0; i < 64; i++) lut[i] = band_from_zigzag_index(i);
    return lut;
}();

struct BandRange {
    int start;  // AC-local start index (0..62)
    int len;    // coefficient count
};

inline BandRange band_ac_range(BandGroup band) {
    switch (band) {
        case BAND_LOW:  return {0, BAND_LOW_END_ZZ};
        case BAND_MID:  return {BAND_LOW_END_ZZ, BAND_MID_END_ZZ - BAND_LOW_END_ZZ};
        case BAND_HIGH: return {BAND_MID_END_ZZ, 63 - BAND_MID_END_ZZ};
        default:        return {0, 0};
    }
}

inline void split_ac_by_band(
    const int16_t quantized[64],
    int16_t low[HAKONYANS_BAND_LOW_END],
    int16_t mid[HAKONYANS_BAND_MID_END - HAKONYANS_BAND_LOW_END],
    int16_t high[63 - HAKONYANS_BAND_MID_END]
) {
    for (int i = 0; i < HAKONYANS_BAND_LOW_END; i++) low[i] = quantized[1 + i];
    for (int i = 0; i < HAKONYANS_BAND_MID_END - HAKONYANS_BAND_LOW_END; i++) {
        mid[i] = quantized[1 + HAKONYANS_BAND_LOW_END + i];
    }
    for (int i = 0; i < 63 - HAKONYANS_BAND_MID_END; i++) {
        high[i] = quantized[1 + HAKONYANS_BAND_MID_END + i];
    }
}

inline int band_magc(uint16_t abs_v) {
    if (abs_v == 0) return 0;
    int magc = 32 - __builtin_clz(abs_v);
    return (magc > 11) ? 11 : magc;
}

inline void tokenize_ac_band(
    const int16_t quantized[64],
    BandGroup band,
    std::vector<Token>& out_tokens
) {
    BandRange r = band_ac_range(band);
    if (r.len <= 0) return;

    int pos = 0;
    while (pos < r.len) {
        int zrun = 0;
        while (pos + zrun < r.len && quantized[1 + r.start + pos + zrun] == 0) zrun++;
        if (pos + zrun == r.len) {
            out_tokens.emplace_back(TokenType::ZRUN_63, 0, 0);
            break;
        }

        out_tokens.emplace_back(static_cast<TokenType>(zrun), 0, 0);
        pos += zrun;

        int16_t v = quantized[1 + r.start + pos];
        uint16_t abs_v = (uint16_t)std::abs(v);
        int magc = band_magc(abs_v);
        uint16_t sign_bit = (v > 0) ? 0 : 1;
        uint16_t rem = (magc > 0) ? (uint16_t)(abs_v - (1u << (magc - 1))) : 0;
        uint16_t raw_bits = (uint16_t)((sign_bit << magc) | rem);
        out_tokens.emplace_back(static_cast<TokenType>(64 + magc), raw_bits, (uint8_t)(1 + magc));
        pos++;
    }

    if (out_tokens.empty() || out_tokens.back().type != TokenType::ZRUN_63) {
        out_tokens.emplace_back(TokenType::ZRUN_63, 0, 0);
    }
}

inline bool detokenize_ac_band_block(
    const std::vector<Token>& tokens,
    size_t& token_pos,
    BandGroup band,
    int16_t ac_coeffs[63]
) {
    BandRange r = band_ac_range(band);
    if (r.len <= 0) return true;

    // Initialize this band's coefficients with zeros
    for (int i = 0; i < r.len; i++) ac_coeffs[r.start + i] = 0;

    int pos = 0;
    while (token_pos < tokens.size() && pos < r.len) {
        const Token& tok = tokens[token_pos++];
        int type = (int)tok.type;
        if (type == 63) {  // legacy compatibility marker
            return true;
        }
        if (type < 0 || type > 62) {
            return false;
        }

        pos += type;
        if (pos >= r.len || token_pos >= tokens.size()) return false;

        const Token& mt = tokens[token_pos++];
        int magc = (int)mt.type - 64;
        if (magc < 0 || magc > 11) return false;
        uint16_t sign = (mt.raw_bits >> magc) & 1u;
        uint16_t rem = mt.raw_bits & ((1u << magc) - 1u);
        uint16_t abs_v = (magc > 0) ? (uint16_t)((1u << (magc - 1)) + rem) : 0;
        ac_coeffs[r.start + pos] = (sign == 0) ? (int16_t)abs_v : (int16_t)-abs_v;
        pos++;
    }

    return (pos == r.len);
}

} // namespace hakonyans
