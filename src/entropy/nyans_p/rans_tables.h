#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include "rans_core.h"

namespace hakonyans {

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
        
        // 合計を RANS_TOTAL に調整（最大頻度のシンボルで吸収）
        if (scaled_total != RANS_TOTAL) {
            int max_idx = std::distance(freq.begin(), 
                std::max_element(freq.begin(), freq.end()));
            freq[max_idx] += RANS_TOTAL - scaled_total;
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
