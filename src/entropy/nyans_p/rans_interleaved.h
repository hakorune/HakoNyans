#pragma once

#include <array>
#include <vector>
#include <span>
#include "rans_core.h"

namespace hakonyans {

/**
 * N=8 インターリーブ rANS エンコーダ
 * 
 * シンボルをラウンドロビンで8状態に割り当て、
 * 依存鎖を切って ILP/SIMD に向く構造にする。
 */
template<int N = 8>
class InterleavedRANSEncoder {
    static_assert(N > 0 && N <= 32, "N must be in [1, 32]");

public:
    InterleavedRANSEncoder() {
        // 状態を初期化
        for (auto& enc : encoders_) {
            enc = RANSEncoder();
        }
    }

    /**
     * シンボルをエンコード（ラウンドロビン割り当て）
     */
    void encode_symbol(const CDFTable& cdf, int symbol) {
        encoders_[current_stream_].encode_symbol(cdf, symbol);
        current_stream_ = (current_stream_ + 1) % N;
    }

    /**
     * エンコード終了、全ストリームを結合
     * 
     * Format:
     *   - 各ストリームの長さ (varint × N)
     *   - ストリーム0のデータ
     *   - ストリーム1のデータ
     *   - ...
     *   - ストリームN-1のデータ
     */
    std::vector<uint8_t> finish() {
        // 各ストリームを finish
        std::array<std::vector<uint8_t>, N> streams;
        for (int i = 0; i < N; ++i) {
            streams[i] = encoders_[i].finish();
        }

        // 合計サイズ計算（長さヘッダ + データ）
        size_t total_size = 0;
        for (int i = 0; i < N; ++i) {
            total_size += 5;  // varint最大5バイト（32bit長）
            total_size += streams[i].size();
        }

        std::vector<uint8_t> output;
        output.reserve(total_size);

        // 長さヘッダ（varint）
        for (int i = 0; i < N; ++i) {
            write_varint(output, streams[i].size());
        }

        // データ
        for (int i = 0; i < N; ++i) {
            output.insert(output.end(), streams[i].begin(), streams[i].end());
        }

        return output;
    }

private:
    std::array<RANSEncoder, N> encoders_;
    int current_stream_ = 0;

    // 簡易 varint エンコード（7bit×N方式）
    static void write_varint(std::vector<uint8_t>& out, uint32_t value) {
        while (value >= 0x80) {
            out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        out.push_back(static_cast<uint8_t>(value));
    }
};

/**
 * N=8 インターリーブ rANS デコーダ
 */
template<int N = 8>
class InterleavedRANSDecoder {
    static_assert(N > 0 && N <= 32, "N must be in [1, 32]");

public:
    /**
     * エンコード済みバイト列から初期化
     */
    explicit InterleavedRANSDecoder(std::span<const uint8_t> encoded) {
        // 長さヘッダを読む
        size_t pos = 0;
        std::array<uint32_t, N> lengths;
        for (int i = 0; i < N; ++i) {
            lengths[i] = read_varint(encoded, pos);
        }

        // 各ストリームのデコーダを初期化
        for (int i = 0; i < N; ++i) {
            size_t stream_end = pos + lengths[i];
            if (stream_end > encoded.size()) {
                throw std::runtime_error("Invalid interleaved stream: length exceeds buffer");
            }
            decoders_[i] = RANSDecoder(encoded.subspan(pos, lengths[i]));
            pos = stream_end;
        }
    }

    /**
     * シンボルをデコード（ラウンドロビン読み出し）
     */
    int decode_symbol(const CDFTable& cdf) {
        int symbol = decoders_[current_stream_].decode_symbol(cdf);
        current_stream_ = (current_stream_ + 1) % N;
        return symbol;
    }

private:
    std::array<RANSDecoder, N> decoders_;
    int current_stream_ = 0;

    // 簡易 varint デコード
    static uint32_t read_varint(std::span<const uint8_t> data, size_t& pos) {
        uint32_t value = 0;
        int shift = 0;
        while (pos < data.size()) {
            uint8_t byte = data[pos++];
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) break;
            shift += 7;
            if (shift >= 32) throw std::runtime_error("Varint overflow");
        }
        return value;
    }
};

} // namespace hakonyans
