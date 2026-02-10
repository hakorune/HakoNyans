#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include <memory>
#include "rans_core.h"

namespace hakonyans {

/**
 * SIMD 向け高速デコードテーブル
 * 
 * slot→symbol の O(1) ルックアップを実現。
 * _mm256_i32gather_epi32 で 8 レーン同時参照可能。
 */
struct alignas(64) SIMDDecodeTable {
    uint32_t slot_to_symbol[RANS_TOTAL];  // 4096 entries: slot → symbol
    uint32_t freq[256];                    // symbol → freq  (最大256シンボル)
    uint32_t bias[256];                    // symbol → cdf[symbol]
    int alphabet_size;
};

/**
 * CDF テーブルビルダー
 * 
 * 頻度を RANS_TOTAL にスケーリングして CDF を構築
 */
class CDFBuilder {
public:
    /**
     * 頻度配列から CDF を構築
     * 頻度は RANS_TOTAL にスケーリングされる
     */
    static CDFTable build_from_freq(const std::vector<uint32_t>& raw_freq) {
        int alphabet_size = raw_freq.size();
        
        // 頻度を RANS_TOTAL にスケーリング
        uint64_t raw_total = 0;
        for (auto f : raw_freq) raw_total += f;
        
        std::vector<uint32_t> freq(alphabet_size);
        uint32_t scaled_total = 0;
        
        for (int i = 0; i < alphabet_size; ++i) {
            // 最低 1 を保証（ゼロ頻度はデコード不可）
            freq[i] = std::max(1U, static_cast<uint32_t>(
                (static_cast<uint64_t>(raw_freq[i]) * RANS_TOTAL + raw_total / 2) / raw_total));
            scaled_total += freq[i];
        }
        
        // 合計を RANS_TOTAL に調整
        while (scaled_total != RANS_TOTAL) {
            if (scaled_total > RANS_TOTAL) {
                // scaled_total が超過 → 頻度が大きいシンボルから1ずつ引く
                for (int i = 0; i < alphabet_size && scaled_total > RANS_TOTAL; ++i) {
                    if (freq[i] > 1) {
                        freq[i]--;
                        scaled_total--;
                    }
                }
            } else {
                // scaled_total が不足 → 最大頻度シンボルに加算
                int max_idx = std::distance(freq.begin(), 
                    std::max_element(freq.begin(), freq.end()));
                freq[max_idx] += RANS_TOTAL - scaled_total;
                scaled_total = RANS_TOTAL;
            }
        }
        
        // CDF 構築
        std::vector<uint32_t> cdf(alphabet_size + 1, 0);
        for (int i = 0; i < alphabet_size; ++i) {
            cdf[i + 1] = cdf[i] + freq[i];
        }
        assert(cdf[alphabet_size] == RANS_TOTAL);
        
        // ヒープに確保
        uint32_t* cdf_ptr = new uint32_t[cdf.size()];
        uint32_t* freq_ptr = new uint32_t[freq.size()];
        std::copy(cdf.begin(), cdf.end(), cdf_ptr);
        std::copy(freq.begin(), freq.end(), freq_ptr);
        
        return CDFTable{
            .total = RANS_TOTAL,
            .cdf = cdf_ptr,
            .freq = freq_ptr,
            .alphabet_size = alphabet_size,
        };
    }

    /**
     * CDFTable から SIMD 向けデコードテーブルを構築
     * 
     * slot→symbol LUT: 各スロット値に対応するシンボルを事前計算
     * freq/bias 配列: gather 命令で 8 レーン同時参照
     */
    static std::unique_ptr<SIMDDecodeTable> build_simd_table(const CDFTable& cdf) {
        auto table = std::make_unique<SIMDDecodeTable>();
        table->alphabet_size = cdf.alphabet_size;
        
        // freq / bias をコピー
        std::memset(table->freq, 0, sizeof(table->freq));
        std::memset(table->bias, 0, sizeof(table->bias));
        for (int i = 0; i < cdf.alphabet_size; ++i) {
            table->freq[i] = cdf.freq[i];
            table->bias[i] = cdf.cdf[i];
        }
        
        // slot→symbol LUT 構築
        for (int sym = 0; sym < cdf.alphabet_size; ++sym) {
            uint32_t lo = cdf.cdf[sym];
            uint32_t hi = cdf.cdf[sym + 1];
            for (uint32_t slot = lo; slot < hi; ++slot) {
                table->slot_to_symbol[slot] = sym;
            }
        }
        
        return table;
    }

    /**
     * 均一分布の CDF を構築
     */
    static CDFTable build_uniform(int alphabet_size) {
        std::vector<uint32_t> freq(alphabet_size, 1);
        return build_from_freq(freq);
    }

    static void cleanup(CDFTable& cdf) {
        delete[] cdf.cdf;
        delete[] cdf.freq;
        cdf.cdf = nullptr;
        cdf.freq = nullptr;
    }
};

}  // namespace hakonyans
