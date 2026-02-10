#pragma once

#ifdef __AVX2__

#include <immintrin.h>
#include <array>
#include <span>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"

namespace hakonyans {

/**
 * AVX2 rANS デコーダ (N=8)
 * 
 * 8 つの rANS 状態を __m256i に格納し、
 * gather + shift + multiply で 8 レーン同時にデコードする。
 * 
 * RANS_TOTAL = 4096 = 2^12 なので:
 *   slot = state & 0xFFF       (ビットAND)
 *   quotient = state >> 12     (右シフト)
 * 除算がゼロ → SIMD の恩恵が最大化
 * 
 * renormalization は各レーンのスカラー処理
 * （データ依存分岐のため完全 SIMD 化は困難）
 */
class AVX2InterleavedDecoder {
public:
    AVX2InterleavedDecoder() : ptr_(nullptr) {}

    explicit AVX2InterleavedDecoder(std::span<const uint8_t> data)
        : data_(data), ptr_(data_.data()) {
        // 先頭 32 バイトから 8 状態を読む（ビッグエンディアン）
        alignas(32) uint32_t init_states[8];
        for (int i = 0; i < 8; ++i) {
            init_states[i] = read_u32_be();
        }
        states_ = _mm256_load_si256(reinterpret_cast<const __m256i*>(init_states));
    }

    /**
     * 8 シンボルを一括デコード
     * 
     * 1回の呼び出しで 8 レーン全てからシンボルを復号。
     * 出力: symbols[0..7] に格納
     */
    void decode_8symbols(const SIMDDecodeTable& tbl, int* symbols_out) {
        const __m256i mask = _mm256_set1_epi32(RANS_TOTAL - 1);
        const __m256i lower_bound = _mm256_set1_epi32(RANS_LOWER_BOUND);

        // 1. slot = state & 0xFFF
        __m256i slots = _mm256_and_si256(states_, mask);

        // 2. symbol = slot_to_symbol[slot]  (8-way gather)
        __m256i syms = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(tbl.slot_to_symbol), slots, 4);

        // 3. freq = freq_table[symbol]  (8-way gather)
        __m256i freqs = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(tbl.freq), syms, 4);

        // 4. bias = bias_table[symbol]  (8-way gather)
        __m256i biases = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(tbl.bias), syms, 4);

        // 5. state = (state >> 12) * freq + slot - bias
        __m256i quotient = _mm256_srli_epi32(states_, RANS_LOG2_TOTAL);
        __m256i new_states = _mm256_mullo_epi32(quotient, freqs);
        new_states = _mm256_add_epi32(new_states, slots);
        new_states = _mm256_sub_epi32(new_states, biases);

        // 6. シンボルをストア
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(symbols_out), syms);

        // 7. Renormalize — 全レーンチェックして不要ならスキップ（高速パス）
        __m256i need_renorm = _mm256_cmpgt_epi32(lower_bound, new_states);
        int renorm_mask = _mm256_movemask_ps(_mm256_castsi256_ps(need_renorm));

        if (__builtin_expect(renorm_mask == 0, 1)) {
            // 高速パス: 全レーン renorm 不要
            states_ = new_states;
        } else {
            // スカラーフォールバック
            alignas(32) uint32_t state_arr[8];
            _mm256_store_si256(reinterpret_cast<__m256i*>(state_arr), new_states);

            for (int i = 0; i < 8; ++i) {
                if (renorm_mask & (1 << i)) {
                    while (state_arr[i] < RANS_LOWER_BOUND) {
                        state_arr[i] = (state_arr[i] << 8) | *ptr_++;
                    }
                }
            }

            states_ = _mm256_load_si256(reinterpret_cast<const __m256i*>(state_arr));
        }
    }

    /**
     * 1 シンボルずつデコード（互換 API）
     * 内部で 8 個ずつバッチ処理し、バッファから返す
     */
    int decode_symbol(const SIMDDecodeTable& tbl) {
        if (buf_pos_ >= 8) {
            decode_8symbols(tbl, buf_);
            buf_pos_ = 0;
        }
        return buf_[buf_pos_++];
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
    __m256i states_;
    int buf_[8] = {};
    int buf_pos_ = 8;  // 初回呼び出しで即デコード
};

} // namespace hakonyans

#endif // __AVX2__
