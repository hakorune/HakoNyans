#pragma once

#include <cstdint>
#include <vector>
#include <span>

namespace hakonyans {

/**
 * BitWriter: 高速なビット単位書き込み
 * 
 * バイト境界効率化：複数ビットを一度に書き込んで
 * メモリ書き込みを最小化
 */
class BitWriter {
public:
    BitWriter() : buffer_(), bit_pos_(0) {}

    /**
     * value の下位 nbits を書き込む
     * 
     * @param value 書き込む値
     * @param nbits ビット数 (1-32)
     */
    void write_bits(uint32_t value, int nbits) {
        value &= (1U << nbits) - 1;  // マスク
        
        while (nbits > 0) {
            int byte_idx = bit_pos_ / 8;
            int bit_off = bit_pos_ % 8;
            
            // バッファを必要なら拡張
            if (byte_idx >= buffer_.size()) {
                buffer_.resize(buffer_.size() * 2 + 16);
            }
            
            // このバイトに書き込める最大ビット数
            int bits_this_byte = 8 - bit_off;
            int bits_to_write = std::min(nbits, bits_this_byte);
            
            // 値をシフトして書き込み
            uint32_t bits = (value >> (nbits - bits_to_write)) & ((1U << bits_to_write) - 1);
            buffer_[byte_idx] |= bits << (8 - bit_off - bits_to_write);
            
            bit_pos_ += bits_to_write;
            nbits -= bits_to_write;
        }
    }

    /**
     * 8-bit 値を書き込み（高速パス）
     */
    void write_byte(uint8_t value) {
        write_bits(value, 8);
    }

    /**
     * バイト境界にアライン（パディング 0）
     */
    void align() {
        if (bit_pos_ % 8 != 0) {
            bit_pos_ += 8 - (bit_pos_ % 8);
        }
    }

    /**
     * 現在の書き込みサイズ（バイト単位）
     */
    size_t size() const {
        return (bit_pos_ + 7) / 8;
    }

    /**
     * バッファをスパンで取得
     */
    std::span<const uint8_t> data() const {
        return std::span(buffer_.data(), size());
    }

    /**
     * バッファをクリア
     */
    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0);
        bit_pos_ = 0;
    }

private:
    std::vector<uint8_t> buffer_;
    size_t bit_pos_;  // ビット単位での書き込み位置
};

}  // namespace hakonyans
