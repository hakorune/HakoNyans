#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>
#include <span>

namespace hakonyans {

/**
 * rANS（Asymmetric Numeral Systems）コア実装
 * 
 * 参考: Jarek Duda, "Asymmetric Numeral Systems" (arXiv:0902.0271)
 *       Fabian Giesen, "Interleaved Entropy Coders" (arXiv:1402.3392)
 * 
 * 重要: rANS はスタック（LIFO）構造。
 * エンコーダはシンボルを逆順に処理し、デコーダは正順に復元する。
 */

constexpr uint32_t RANS_LOG2_TOTAL = 12;  // CDF total のビット幅
constexpr uint32_t RANS_TOTAL = 1U << RANS_LOG2_TOTAL;  // 4096
constexpr uint32_t RANS_LOWER_BOUND = 1U << 16;  // state の下限

/**
 * CDF（Cumulative Distribution Function）テーブル
 */
struct CDFTable {
    uint32_t total;              // = RANS_TOTAL（固定）
    const uint32_t* cdf;         // CDF[i] = sum(freq[0..i-1])
    const uint32_t* freq;        // 各シンボルの頻度（合計 = total）
    int alphabet_size;
};

/**
 * rANS エンコーダ
 * 
 * 使い方:
 *   1. encode_symbol() でシンボルを追加（内部で逆順処理）
 *   2. finish() でバイト列を取得
 */
class RANSEncoder {
public:
    RANSEncoder() : state_(RANS_LOWER_BOUND) {}

    /**
     * シンボルをエンコード（バッファに蓄積、finish で一括処理）
     */
    void encode_symbol(const CDFTable& cdf, int symbol) {
        assert(symbol >= 0 && symbol < cdf.alphabet_size);
        pending_.push_back({&cdf, symbol});
    }

    /**
     * エンコード完了、バイト列を返す
     * 
     * 蓄積したシンボルを逆順に処理（rANS の LIFO 特性）
     */
    std::vector<uint8_t> finish() {
        std::vector<uint8_t> out_bytes;
        
        // シンボルを逆順に処理
        for (int i = static_cast<int>(pending_.size()) - 1; i >= 0; --i) {
            const CDFTable& cdf = *pending_[i].cdf;
            int symbol = pending_[i].symbol;
            
            uint32_t freq = cdf.freq[symbol];
            uint32_t bias = cdf.cdf[symbol];
            
            // Pre-renormalize: state が大きすぎる場合にバイトを吐く
            // エンコード後に state が上限を超えないよう事前に縮小
            uint32_t max_state = ((RANS_LOWER_BOUND >> RANS_LOG2_TOTAL) << 8) * freq;
            while (state_ >= max_state) {
                out_bytes.push_back(state_ & 0xFF);
                state_ >>= 8;
            }
            
            // コアエンコード: state = (state / freq) * total + (state % freq) + bias
            state_ = (state_ / freq) * cdf.total + (state_ % freq) + bias;
        }
        
        // 最終状態を 4 バイトで出力（リトルエンディアン）
        out_bytes.push_back((state_ >> 0) & 0xFF);
        out_bytes.push_back((state_ >> 8) & 0xFF);
        out_bytes.push_back((state_ >> 16) & 0xFF);
        out_bytes.push_back((state_ >> 24) & 0xFF);
        
        // バッファを反転（デコーダは先頭から読む）
        std::reverse(out_bytes.begin(), out_bytes.end());
        
        pending_.clear();
        return out_bytes;
    }

    void reset() {
        state_ = RANS_LOWER_BOUND;
        pending_.clear();
    }

private:
    struct PendingSymbol {
        const CDFTable* cdf;
        int symbol;
    };
    
    uint32_t state_;
    std::vector<PendingSymbol> pending_;
};

/**
 * rANS デコーダ
 */
class RANSDecoder {
public:
    RANSDecoder() : state_(0), data_(), pos_(0) {}
    
    explicit RANSDecoder(std::span<const uint8_t> data) 
        : data_(data.begin(), data.end()), pos_(0) {
        // 最初の 4 バイトで state を初期化（ビッグエンディアン：反転後の先頭）
        state_ = read_u32();
    }

    /**
     * シンボルをデコード
     */
    int decode_symbol(const CDFTable& cdf) {
        // slot = state % total でシンボル特定
        uint32_t slot = state_ % cdf.total;
        
        // CDF テーブルから symbol を線形探索
        int symbol = 0;
        for (int i = 0; i < cdf.alphabet_size; ++i) {
            if (slot < cdf.cdf[i + 1]) {
                symbol = i;
                break;
            }
        }
        
        uint32_t freq = cdf.freq[symbol];
        uint32_t bias = cdf.cdf[symbol];
        
        // state 更新: encode の逆操作
        state_ = (state_ / cdf.total) * freq + (state_ % cdf.total) - bias;
        
        // Renormalize: state が下限を下回ったらバイトを読む
        while (state_ < RANS_LOWER_BOUND && pos_ < data_.size()) {
            state_ = (state_ << 8) | data_[pos_++];
        }
        
        return symbol;
    }

    bool eof() const {
        return pos_ >= data_.size();
    }

private:
    uint32_t read_u32() {
        uint32_t v = 0;
        v |= static_cast<uint32_t>(data_[pos_++]) << 24;
        v |= static_cast<uint32_t>(data_[pos_++]) << 16;
        v |= static_cast<uint32_t>(data_[pos_++]) << 8;
        v |= static_cast<uint32_t>(data_[pos_++]) << 0;
        return v;
    }

    uint32_t state_;
    std::span<const uint8_t> data_;  // ← ゼロコピー（vector から span に変更）
    size_t pos_;
};

}  // namespace hakonyans
