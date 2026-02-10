#pragma once

#include <array>
#include <vector>
#include <span>
#include <cstring>
#include "rans_core.h"
#include "rans_tables.h"

namespace hakonyans {

/**
 * Flat Interleaved rANS エンコーダ (N=8)
 * 
 * 8状態が「1本のバイトストリーム」を共有する。
 * これにより、デコーダ側でSIMD化が可能になる：
 * - renorm バイトの読み出しが連続アドレスになる
 * - 8 状態の算術を同時に実行できる
 * 
 * エンコード形式:
 *   [state0..state7 : 32 bytes] [renorm bytes...]
 */
template<int N = 8>
class FlatInterleavedEncoder {
    static_assert(N > 0 && N <= 8, "N must be in [1, 8]");

public:
    FlatInterleavedEncoder() {
        states_.fill(RANS_LOWER_BOUND);
    }

    void encode_symbol(const CDFTable& cdf, int symbol) {
        pending_.push_back({&cdf, symbol});
    }

    /**
     * エンコード完了
     * 
     * LIFO 処理: 最後のシンボルから逆順に処理し、
     * renorm バイトは共有バッファに出力。
     * 最後にバッファ全体を反転してデコーダが先頭から読めるようにする。
     */
    std::vector<uint8_t> finish() {
        std::vector<uint8_t> out;
        out.reserve(pending_.size());  // rough estimate

        // 逆順処理（LIFO）
        for (int i = static_cast<int>(pending_.size()) - 1; i >= 0; --i) {
            int lane = i % N;
            const CDFTable& cdf = *pending_[i].cdf;
            int symbol = pending_[i].symbol;

            uint32_t freq = cdf.freq[symbol];
            uint32_t bias = cdf.cdf[symbol];

            // Pre-renormalize
            uint32_t max_state = ((RANS_LOWER_BOUND >> RANS_LOG2_TOTAL) << 8) * freq;
            while (states_[lane] >= max_state) {
                out.push_back(states_[lane] & 0xFF);
                states_[lane] >>= 8;
            }

            // Core encode
            states_[lane] = (states_[lane] / freq) * cdf.total
                          + (states_[lane] % freq) + bias;
        }

        // 8 つの最終状態を逆順で出力（buffer反転後に state0 が先頭に来る）
        for (int i = N - 1; i >= 0; --i) {
            out.push_back((states_[i] >>  0) & 0xFF);
            out.push_back((states_[i] >>  8) & 0xFF);
            out.push_back((states_[i] >> 16) & 0xFF);
            out.push_back((states_[i] >> 24) & 0xFF);
        }

        // 反転してデコーダが先頭から読めるようにする
        std::reverse(out.begin(), out.end());

        pending_.clear();
        return out;
    }

private:
    struct PendingSymbol {
        const CDFTable* cdf;
        int symbol;
    };

    std::array<uint32_t, N> states_;
    std::vector<PendingSymbol> pending_;
};

/**
 * Flat Interleaved rANS デコーダ（スカラーリファレンス実装）
 * 
 * 8 状態が 1 本のバイトストリームを共有。
 * AVX2 版の golden reference として使う。
 */
template<int N = 8>
class FlatInterleavedDecoder {
    static_assert(N > 0 && N <= 8, "N must be in [1, 8]");

public:
    FlatInterleavedDecoder() : ptr_(nullptr) {}

    explicit FlatInterleavedDecoder(std::span<const uint8_t> data)
        : data_(data), ptr_(data_.data()) {
        // 先頭 N×4 バイトから状態を読む（ビッグエンディアン：反転後）
        for (int i = 0; i < N; ++i) {
            states_[i] = read_u32_be();
        }
    }

    int decode_symbol(const CDFTable& cdf) {
        int lane = current_lane_;
        current_lane_ = (current_lane_ + 1) % N;

        // slot = state & (RANS_TOTAL - 1)  ← total が 2^12 なのでビットAND
        uint32_t slot = states_[lane] & (RANS_TOTAL - 1);

        // 線形探索でシンボル特定
        int symbol = 0;
        for (int i = 0; i < cdf.alphabet_size; ++i) {
            if (slot < cdf.cdf[i + 1]) { symbol = i; break; }
        }

        uint32_t freq = cdf.freq[symbol];
        uint32_t bias = cdf.cdf[symbol];

        // state 更新
        states_[lane] = (states_[lane] >> RANS_LOG2_TOTAL) * freq + slot - bias;

        // Renormalize: 共有ストリームからバイトを読む
        while (states_[lane] < RANS_LOWER_BOUND) {
            states_[lane] = (states_[lane] << 8) | *ptr_++;
        }

        return symbol;
    }

    /**
     * LUT ベースの高速デコード（スカラー）
     */
    int decode_symbol_lut(const SIMDDecodeTable& tbl) {
        int lane = current_lane_;
        current_lane_ = (current_lane_ + 1) % N;

        uint32_t slot = states_[lane] & (RANS_TOTAL - 1);
        uint32_t symbol = tbl.slot_to_symbol[slot];
        uint32_t freq = tbl.freq[symbol];
        uint32_t bias = tbl.bias[symbol];

        states_[lane] = (states_[lane] >> RANS_LOG2_TOTAL) * freq + slot - bias;

        while (states_[lane] < RANS_LOWER_BOUND) {
            states_[lane] = (states_[lane] << 8) | *ptr_++;
        }

        return static_cast<int>(symbol);
    }

private:
    uint32_t read_u32_be() {
        uint32_t v = 0;
        v |= static_cast<uint32_t>(*ptr_++) << 24;
        v |= static_cast<uint32_t>(*ptr_++) << 16;
        v |= static_cast<uint32_t>(*ptr_++) << 8;
        v |= static_cast<uint32_t>(*ptr_++) << 0;
        return v;
    }

    std::span<const uint8_t> data_;
    const uint8_t* ptr_;
    std::array<uint32_t, N> states_;
    int current_lane_ = 0;
};

} // namespace hakonyans
