#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <array>
#include <cassert>
#include "rans_core.h"

namespace hakonyans {

/**
 * P-Index チェックポイント
 * 
 * ビットストリーム中の任意地点の rANS 状態スナップショット。
 * これにより、任意のチェックポイントから独立にデコードを開始できる。
 */
struct Checkpoint {
    uint32_t byte_offset;       // CoreBitstream 内のバイト位置
    uint32_t token_index;       // トークン列内の位置
    std::array<uint32_t, 8> states;  // N=8 の rANS 中間状態
};

/**
 * P-Index: チェックポイント列
 */
struct PIndex {
    std::vector<Checkpoint> checkpoints;
    uint32_t total_tokens;      // 全トークン数
    uint32_t total_bytes;       // CoreBitstream の全バイト数
};

/**
 * P-Index 付き Flat Interleaved エンコーダ
 * 
 * 通常のエンコードに加えて、一定間隔でチェックポイントを記録。
 * チェックポイントはデコード時のスナップショットなので、
 * 「デコードしながら記録」する必要がある → エンコード後にデコードパスで収集。
 */
class PIndexBuilder {
public:
    /**
     * エンコード済みストリームからチェックポイントを収集
     * 
     * @param encoded エンコード済みバイト列（FlatInterleavedEncoder の出力）
     * @param cdf 使用した CDF テーブル
     * @param total_tokens 全トークン数
     * @param interval チェックポイント間隔（トークン数、8の倍数）
     */
    static PIndex build(std::span<const uint8_t> encoded,
                        const CDFTable& cdf,
                        uint32_t total_tokens,
                        uint32_t interval = 8192) {
        // interval は 8 の倍数に切り上げ
        interval = ((interval + 7) / 8) * 8;
        
        PIndex pindex;
        pindex.total_tokens = total_tokens;
        pindex.total_bytes = encoded.size();
        
        // 先頭チェックポイント（常に存在）
        const uint8_t* ptr = encoded.data();
        std::array<uint32_t, 8> states;
        for (int i = 0; i < 8; ++i) {
            uint32_t v = 0;
            v |= static_cast<uint32_t>(*ptr++) << 24;
            v |= static_cast<uint32_t>(*ptr++) << 16;
            v |= static_cast<uint32_t>(*ptr++) << 8;
            v |= static_cast<uint32_t>(*ptr++) << 0;
            states[i] = v;
        }
        
        pindex.checkpoints.push_back(Checkpoint{
            .byte_offset = 0,
            .token_index = 0,
            .states = states,
        });
        
        // デコードしながらチェックポイントを収集
        uint32_t token_pos = 0;
        while (token_pos < total_tokens) {
            uint32_t batch_end = std::min(token_pos + interval, total_tokens);
            
            // この区間をデコード（状態を更新）
            while (token_pos < batch_end) {
                int lane = token_pos % 8;
                
                uint32_t slot = states[lane] & (RANS_TOTAL - 1);
                
                // CDF から symbol 特定
                int symbol = 0;
                for (int i = 0; i < cdf.alphabet_size; ++i) {
                    if (slot < cdf.cdf[i + 1]) { symbol = i; break; }
                }
                
                uint32_t freq = cdf.freq[symbol];
                uint32_t bias = cdf.cdf[symbol];
                
                states[lane] = (states[lane] >> RANS_LOG2_TOTAL) * freq + slot - bias;
                
                while (states[lane] < RANS_LOWER_BOUND) {
                    states[lane] = (states[lane] << 8) | *ptr++;
                }
                
                token_pos++;
            }
            
            // チェックポイント記録（最終地点は不要）
            if (token_pos < total_tokens) {
                pindex.checkpoints.push_back(Checkpoint{
                    .byte_offset = static_cast<uint32_t>(ptr - encoded.data()),
                    .token_index = token_pos,
                    .states = states,
                });
            }
        }
        
        return pindex;
    }
};

/**
 * P-Index のシリアライズ/デシリアライズ
 */
class PIndexCodec {
public:
    /**
     * P-Index をバイト列にシリアライズ
     * 
     * Format:
     *   [total_tokens: u32] [total_bytes: u32] [num_checkpoints: u32]
     *   For each checkpoint:
     *     [byte_offset: u32] [token_index: u32] [states: u32×8]
     */
    static std::vector<uint8_t> serialize(const PIndex& pindex) {
        size_t size = 12 + pindex.checkpoints.size() * (8 + 32);
        std::vector<uint8_t> out;
        out.reserve(size);
        
        write_u32(out, pindex.total_tokens);
        write_u32(out, pindex.total_bytes);
        write_u32(out, pindex.checkpoints.size());
        
        for (const auto& cp : pindex.checkpoints) {
            write_u32(out, cp.byte_offset);
            write_u32(out, cp.token_index);
            for (int i = 0; i < 8; ++i) {
                write_u32(out, cp.states[i]);
            }
        }
        
        return out;
    }
    
    static PIndex deserialize(std::span<const uint8_t> data) {
        PIndex pindex;
        size_t pos = 0;
        
        pindex.total_tokens = read_u32(data, pos);
        pindex.total_bytes = read_u32(data, pos);
        uint32_t num_cp = read_u32(data, pos);
        
        pindex.checkpoints.resize(num_cp);
        for (uint32_t i = 0; i < num_cp; ++i) {
            pindex.checkpoints[i].byte_offset = read_u32(data, pos);
            pindex.checkpoints[i].token_index = read_u32(data, pos);
            for (int j = 0; j < 8; ++j) {
                pindex.checkpoints[i].states[j] = read_u32(data, pos);
            }
        }
        
        return pindex;
    }

private:
    static void write_u32(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back((v >>  0) & 0xFF);
        out.push_back((v >>  8) & 0xFF);
        out.push_back((v >> 16) & 0xFF);
        out.push_back((v >> 24) & 0xFF);
    }
    
    static uint32_t read_u32(std::span<const uint8_t> data, size_t& pos) {
        uint32_t v = 0;
        v |= static_cast<uint32_t>(data[pos++]) <<  0;
        v |= static_cast<uint32_t>(data[pos++]) <<  8;
        v |= static_cast<uint32_t>(data[pos++]) << 16;
        v |= static_cast<uint32_t>(data[pos++]) << 24;
        return v;
    }
};

} // namespace hakonyans
