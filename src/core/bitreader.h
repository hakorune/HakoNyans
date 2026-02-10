#pragma once

#include <cstdint>
#include <span>

namespace hakonyans {

/**
 * BitReader: 高速なビット単位読み込み
 * 
 * BitWriter で書き込まれたビットストリームを復号
 */
class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> data) 
        : data_(data), bit_pos_(0) {}

    /**
     * nbits を読み込んで返す
     * 
     * @param nbits ビット数 (1-32)
     * @return 読み込まれた値
     */
    uint32_t read_bits(int nbits) {
        uint32_t result = 0;
        
        while (nbits > 0) {
            int byte_idx = bit_pos_ / 8;
            int bit_off = bit_pos_ % 8;
            
            // 境界チェック
            if (byte_idx >= data_.size()) {
                return 0;  // エラー（簡易処理）
            }
            
            // このバイトから読める最大ビット数
            int bits_this_byte = 8 - bit_off;
            int bits_to_read = std::min(nbits, bits_this_byte);
            
            // ビットを抽出
            uint8_t byte_val = data_[byte_idx];
            uint32_t bits = (byte_val >> (8 - bit_off - bits_to_read)) & ((1U << bits_to_read) - 1);
            
            // 結果に追加
            result = (result << bits_to_read) | bits;
            
            bit_pos_ += bits_to_read;
            nbits -= bits_to_read;
        }
        
        return result;
    }

    /**
     * 8-bit 値を読み込み（高速パス）
     */
    uint8_t read_byte() {
        return static_cast<uint8_t>(read_bits(8));
    }

    /**
     * バイト境界にアライン
     */
    void align() {
        if (bit_pos_ % 8 != 0) {
            bit_pos_ += 8 - (bit_pos_ % 8);
        }
    }

    /**
     * 現在の読み込み位置（ビット単位）
     */
    size_t tell() const {
        return bit_pos_;
    }

    /**
     * 読み込み位置をリセット
     */
    void reset() {
        bit_pos_ = 0;
    }

    /**
     * EOF判定
     */
    bool eof() const {
        return bit_pos_ / 8 >= data_.size();
    }

private:
    std::span<const uint8_t> data_;
    size_t bit_pos_;  // ビット単位での読み込み位置
};

}  // namespace hakonyans
