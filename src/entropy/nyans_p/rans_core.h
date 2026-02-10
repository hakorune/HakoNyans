#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include "../../core/bitwriter.h"
#include "../../core/bitreader.h"

namespace hakonyans {

/**
 * rANS（Asymmetric Numeral Systems）コア実装
 * 
 * 参考: Jarek Duda, "Asymmetric Numeral Systems" (arXiv:0902.0271)
 * 
 * 単一の 32-bit 状態で、レンジ符号に匹敵する圧縮率と
 * ハフマンに近い高速復号を実現。
 */

// rANS 状態のビット幅
constexpr int RANS_STATE_BITS = 32;
constexpr uint32_t RANS_LOWER_BOUND = 1U << 16;  // 復号化の下限

/**
 * CDF（Cumulative Distribution Function）テーブル
 * 
 * 例：3 シンボル、確率 1/4, 1/2, 1/4 の場合
 *   cdf[] = {0, 1, 3, 4}  (cdf[i] = 合計頻度)
 *   freq[] = {1, 2, 1}
 */
struct CDFTable {
    uint32_t total;              // CDF の最大値（全シンボルの合計頻度）
    const uint32_t* cdf;         // CDF 値のテーブル
    const uint32_t* freq;        // 各シンボルの頻度
    int alphabet_size;           // シンボル数
};

/**
 * rANS エンコーダ
 */
class RANSEncoder {
public:
    RANSEncoder() : state_(RANS_LOWER_BOUND), writer_() {}

    /**
     * シンボルをエンコード
     * 
     * @param cdf CDF テーブル
     * @param symbol シンボル (0 to alphabet_size-1)
     */
    void encode_symbol(const CDFTable& cdf, int symbol) {
        assert(symbol >= 0 && symbol < cdf.alphabet_size);
        
        uint32_t freq = cdf.freq[symbol];
        uint32_t bias = cdf.cdf[symbol];
        
        // state = (state / freq) * total + (state % freq) + bias
        uint64_t q = state_ / freq;
        uint32_t r = state_ % freq;
        state_ = q * cdf.total + r + bias;
        
        // 正規化：state がアンダーフロー範囲に入ったらバイトを吐く
        while (state_ < RANS_LOWER_BOUND) {
            writer_.write_byte(state_ & 0xFF);
            state_ >>= 8;
        }
    }

    /**
     * エンコード終了、バッファを返す
     */
    std::vector<uint8_t> finish() {
        // 最終状態を 4 バイトで書き込み
        writer_.write_bits((state_ >> 0) & 0xFF, 8);
        writer_.write_bits((state_ >> 8) & 0xFF, 8);
        writer_.write_bits((state_ >> 16) & 0xFF, 8);
        writer_.write_bits((state_ >> 24) & 0xFF, 8);
        
        std::vector<uint8_t> result(writer_.data().begin(), writer_.data().end());
        return result;
    }

    void reset() {
        state_ = RANS_LOWER_BOUND;
        writer_.reset();
    }

private:
    uint32_t state_;
    BitWriter writer_;
};

/**
 * rANS デコーダ
 */
class RANSDecoder {
public:
    RANSDecoder() : state_(0), reader_(std::span<const uint8_t>()) {}
    
    explicit RANSDecoder(std::span<const uint8_t> data) : reader_(data) {
        // 最初の 4 バイトで状態を初期化
        // エンコーダが finish() で 32-bit を書いた順に読む
        uint32_t b0 = reader_.read_byte();
        uint32_t b1 = reader_.read_byte();
        uint32_t b2 = reader_.read_byte();
        uint32_t b3 = reader_.read_byte();
        state_ = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
    }

    /**
     * シンボルをデコード
     * 
     * @param cdf CDF テーブル
     * @return デコードされたシンボル
     */
    int decode_symbol(const CDFTable& cdf) {
        // slot = state % total でシンボルを特定
        uint32_t slot = state_ % cdf.total;
        
        // CDF テーブルから symbol を線形探索
        // （本来は二分探索か alias method を使うが、簡易実装）
        int symbol = 0;
        for (int i = 0; i < cdf.alphabet_size; ++i) {
            if (slot < cdf.cdf[i + 1]) {
                symbol = i;
                break;
            }
        }
        
        uint32_t freq = cdf.freq[symbol];
        uint32_t bias = cdf.cdf[symbol];
        
        // state 更新
        state_ = (state_ / cdf.total) * freq + (state_ % cdf.total) - bias;
        
        // 正規化：state がアンダーフロー範囲に入ったらバイトを読む
        while (state_ < RANS_LOWER_BOUND) {
            state_ = (state_ << 8) | reader_.read_byte();
        }
        
        return symbol;
    }

    /**
     * EOF 判定
     */
    bool eof() const {
        return reader_.eof();
    }

    void reset(std::span<const uint8_t> data) {
        reader_ = BitReader(data);
        // 最初の 4 バイトで再初期化
        uint32_t b0 = reader_.read_byte();
        uint32_t b1 = reader_.read_byte();
        uint32_t b2 = reader_.read_byte();
        uint32_t b3 = reader_.read_byte();
        state_ = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
    }

private:
    uint32_t state_;
    BitReader reader_;
};

}  // namespace hakonyans
