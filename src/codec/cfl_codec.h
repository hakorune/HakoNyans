#pragma once

#include "headers.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace hakonyans::cfl_codec {

/**
 * Extract CFL payload size from tile data.
 * Supports both legacy (8-field) and band-group (10-field) tile headers.
 */
inline uint32_t extract_tile_cfl_size(const std::vector<uint8_t>& tile_data, bool use_band_group_cdf) {
    if (use_band_group_cdf) {
        if (tile_data.size() < 40) return 0;
        uint32_t sz[10];
        std::memcpy(sz, tile_data.data(), 40);
        return sz[6];
    }
    if (tile_data.size() < 32) return 0;
    uint32_t sz[8];
    std::memcpy(sz, tile_data.data(), 32);
    return sz[4];
}

/**
 * Legacy CFL serialization: 2 bytes per block (alpha_q6, beta_legacy).
 * Predictor: pred = a*y + b_legacy
 */
inline std::vector<uint8_t> serialize_cfl_legacy(const std::vector<CfLParams>& cfl_params) {
    std::vector<uint8_t> out;
    if (cfl_params.empty()) return out;
    out.reserve(cfl_params.size() * 2);
    for (const auto& p : cfl_params) {
        int a_q6 = (int)std::lround(std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f));
        int b_center = (int)std::lround(std::clamp(p.beta_cb, 0.0f, 255.0f));
        // Legacy predictor: pred = a*y + b_legacy
        // Current centered model: pred = a*(y-128) + b_center
        int b_legacy = std::clamp(b_center - 2 * a_q6, 0, 255);
        out.push_back(static_cast<uint8_t>(static_cast<int8_t>(a_q6)));
        out.push_back(static_cast<uint8_t>(b_legacy));
    }
    return out;
}

/**
 * Adaptive CFL serialization: bitmask + 2 bytes per applied block.
 * Only encodes blocks where alpha_cr > 0.5f.
 */
inline std::vector<uint8_t> serialize_cfl_adaptive(const std::vector<CfLParams>& cfl_params) {
    std::vector<uint8_t> out;
    if (cfl_params.empty()) return out;

    const int nb = (int)cfl_params.size();
    int applied_count = 0;
    for (const auto& p : cfl_params) {
        if (p.alpha_cr > 0.5f) applied_count++;
    }
    if (applied_count == 0) return out;

    const size_t mask_bytes = ((size_t)nb + 7) / 8;
    out.resize(mask_bytes, 0);
    out.reserve(mask_bytes + (size_t)applied_count * 2);
    for (int i = 0; i < nb; i++) {
        if (cfl_params[i].alpha_cr > 0.5f) {
            out[(size_t)i / 8] |= (uint8_t)(1u << (i % 8));
            int a_q6 = (int)std::lround(std::clamp(cfl_params[i].alpha_cb * 64.0f, -128.0f, 127.0f));
            int b = (int)std::lround(std::clamp(cfl_params[i].beta_cb, 0.0f, 255.0f));
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(a_q6)));
            out.push_back(static_cast<uint8_t>(b));
        }
    }
    return out;
}

/**
 * Build CFL payload with automatic mode selection.
 * Chooses adaptive serialization when beneficial, falls back to legacy
 * when sizes would collide (decode-side ambiguity).
 */
inline std::vector<uint8_t> build_cfl_payload(const std::vector<CfLParams>& cfl_params) {
    if (cfl_params.empty()) return {};
    bool any_applied = false;
    for (const auto& p : cfl_params) {
        if (p.alpha_cr > 0.5f) {
            any_applied = true;
            break;
        }
    }
    if (!any_applied) return {};

    auto adaptive = serialize_cfl_adaptive(cfl_params);
    const size_t legacy_size = cfl_params.size() * 2;
    // If sizes collide, keep legacy to avoid decode-side ambiguity.
    if (!adaptive.empty() && adaptive.size() != legacy_size) {
        return adaptive;
    }

    // Safe fallback for ambiguous sizes.
    return serialize_cfl_legacy(cfl_params);
}

} // namespace hakonyans::cfl_codec
