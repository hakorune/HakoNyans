#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include "rans_core.h"

namespace hakonyans {

/**
 * シンプルな CDF テーブルビルダー
 * 
 * 確率分布からエンコード/デコードに必要な
 * CDF テーブルを構築
 */
class CDFBuilder {
public:
    /**
     * 頻度配列から CDF を構築
     * 
     * @param freq 各シンボルの頻度配列
     * @return CDFTable
     */
    static CDFTable build_from_freq(const std::vector<uint32_t>& freq) {
        int alphabet_size = freq.size();
        
        // CDF テーブル（累積和）
        std::vector<uint32_t> cdf(alphabet_size + 1, 0);
        uint32_t total = 0;
        for (int i = 0; i < alphabet_size; ++i) {
            total += freq[i];
            cdf[i + 1] = total;
        }
        
        // 動的メモリに確保（呼び出し側が管理する必要がある）
        uint32_t* cdf_ptr = new uint32_t[cdf.size()];
        uint32_t* freq_ptr = new uint32_t[freq.size()];
        
        std::copy(cdf.begin(), cdf.end(), cdf_ptr);
        std::copy(freq.begin(), freq.end(), freq_ptr);
        
        return CDFTable{
            .total = total,
            .cdf = cdf_ptr,
            .freq = freq_ptr,
            .alphabet_size = alphabet_size,
        };
    }

    /**
     * 均一分布の CDF を構築
     * 
     * @param alphabet_size シンボル数
     * @return CDFTable
     */
    static CDFTable build_uniform(int alphabet_size) {
        std::vector<uint32_t> freq(alphabet_size, 1);
        return build_from_freq(freq);
    }

    /**
     * CDF テーブルをクリーンアップ
     */
    static void cleanup(CDFTable& cdf) {
        delete[] cdf.cdf;
        delete[] cdf.freq;
        cdf.cdf = nullptr;
        cdf.freq = nullptr;
    }
};

}  // namespace hakonyans
