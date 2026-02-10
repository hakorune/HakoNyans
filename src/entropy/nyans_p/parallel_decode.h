#pragma once

#include <vector>
#include <span>
#include <cstdint>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"
#include "src/entropy/nyans_p/pindex.h"
#include "src/platform/thread_pool.h"

namespace hakonyans {

/**
 * P-Index 並列デコーダ
 * 
 * チェックポイントで区間分割し、各スレッドが独立にデコード。
 * デコーダ側のコア数に合わせて並列度を調整できる。
 */
class ParallelDecoder {
public:
    /**
     * 並列デコード
     * 
     * @param encoded エンコード済みバイト列
     * @param pindex チェックポイント情報
     * @param cdf CDF テーブル
     * @param num_threads スレッド数 (0=auto)
     * @return デコードされたシンボル列
     */
    static std::vector<int> decode(
        std::span<const uint8_t> encoded,
        const PIndex& pindex,
        const CDFTable& cdf,
        int num_threads = 0)
    {
        ThreadPool pool(num_threads);
        return decode_with_pool(encoded, pindex, cdf, pool);
    }

    /**
     * 既存のスレッドプールを使って並列デコード
     */
    static std::vector<int> decode_with_pool(
        std::span<const uint8_t> encoded,
        const PIndex& pindex,
        const CDFTable& cdf,
        ThreadPool& pool)
    {
        int num_threads = pool.num_threads();
        int num_checkpoints = pindex.checkpoints.size();
        
        // 出力バッファ
        std::vector<int> output(pindex.total_tokens);
        
        if (num_checkpoints <= 1 || num_threads <= 1) {
            // シングルスレッドフォールバック
            decode_segment(encoded, pindex.checkpoints[0],
                           pindex.total_tokens, cdf,
                           output.data());
            return output;
        }
        
        // チェックポイントをスレッド数に合わせて分割
        // 各スレッドに1つ以上のチェックポイント区間を割り当て
        int segments = std::min(num_threads, num_checkpoints);
        int cp_per_thread = num_checkpoints / segments;
        
        std::vector<std::future<void>> futures;
        
        for (int t = 0; t < segments; ++t) {
            int cp_start = t * cp_per_thread;
            int cp_end = (t == segments - 1) ? num_checkpoints : (t + 1) * cp_per_thread;
            
            const Checkpoint& start_cp = pindex.checkpoints[cp_start];
            
            // この区間のトークン数を計算
            uint32_t token_start = start_cp.token_index;
            uint32_t token_end;
            if (cp_end < num_checkpoints) {
                token_end = pindex.checkpoints[cp_end].token_index;
            } else {
                token_end = pindex.total_tokens;
            }
            uint32_t segment_tokens = token_end - token_start;
            
            futures.push_back(pool.submit([&encoded, &start_cp, segment_tokens,
                                           token_start, &cdf, &output] {
                decode_segment(encoded, start_cp, segment_tokens, cdf,
                               output.data() + token_start);
            }));
        }
        
        // 全スレッド完了待ち
        for (auto& f : futures) f.get();
        
        return output;
    }

    /**
     * LUT ベースの並列デコード（高速版）
     */
    static std::vector<int> decode_lut(
        std::span<const uint8_t> encoded,
        const PIndex& pindex,
        const SIMDDecodeTable& tbl,
        int num_threads = 0)
    {
        ThreadPool pool(num_threads);
        return decode_lut_with_pool(encoded, pindex, tbl, pool);
    }

    static std::vector<int> decode_lut_with_pool(
        std::span<const uint8_t> encoded,
        const PIndex& pindex,
        const SIMDDecodeTable& tbl,
        ThreadPool& pool)
    {
        int num_threads = pool.num_threads();
        int num_checkpoints = pindex.checkpoints.size();
        
        std::vector<int> output(pindex.total_tokens);
        
        if (num_checkpoints <= 1 || num_threads <= 1) {
            decode_segment_lut(encoded, pindex.checkpoints[0],
                               pindex.total_tokens, tbl,
                               output.data());
            return output;
        }
        
        int segments = std::min(num_threads, num_checkpoints);
        int cp_per_thread = num_checkpoints / segments;
        
        std::vector<std::future<void>> futures;
        
        for (int t = 0; t < segments; ++t) {
            int cp_start = t * cp_per_thread;
            int cp_end = (t == segments - 1) ? num_checkpoints : (t + 1) * cp_per_thread;
            
            const Checkpoint& start_cp = pindex.checkpoints[cp_start];
            uint32_t token_start = start_cp.token_index;
            uint32_t token_end = (cp_end < num_checkpoints)
                ? pindex.checkpoints[cp_end].token_index
                : pindex.total_tokens;
            uint32_t segment_tokens = token_end - token_start;
            
            futures.push_back(pool.submit([&encoded, &start_cp, segment_tokens,
                                           token_start, &tbl, &output] {
                decode_segment_lut(encoded, start_cp, segment_tokens, tbl,
                                   output.data() + token_start);
            }));
        }
        
        for (auto& f : futures) f.get();
        return output;
    }

private:
    /**
     * チェックポイントからの区間デコード（CDF 版）
     */
    static void decode_segment(
        std::span<const uint8_t> encoded,
        const Checkpoint& cp,
        uint32_t num_tokens,
        const CDFTable& cdf,
        int* output)
    {
        // チェックポイントから状態を復元
        std::array<uint32_t, 8> states = cp.states;
        const uint8_t* ptr;
        
        if (cp.byte_offset == 0) {
            ptr = encoded.data() + 32;  // 先頭: 8×4バイトの状態ヘッダをスキップ
        } else {
            ptr = encoded.data() + cp.byte_offset;  // 既に絶対位置
        }
        
        for (uint32_t i = 0; i < num_tokens; ++i) {
            int lane = i % 8;
            
            uint32_t slot = states[lane] & (RANS_TOTAL - 1);
            
            int symbol = 0;
            for (int s = 0; s < cdf.alphabet_size; ++s) {
                if (slot < cdf.cdf[s + 1]) { symbol = s; break; }
            }
            
            uint32_t freq = cdf.freq[symbol];
            uint32_t bias = cdf.cdf[symbol];
            
            states[lane] = (states[lane] >> RANS_LOG2_TOTAL) * freq + slot - bias;
            
            while (states[lane] < RANS_LOWER_BOUND) {
                states[lane] = (states[lane] << 8) | *ptr++;
            }
            
            output[i] = symbol;
        }
    }

    /**
     * チェックポイントからの区間デコード（LUT 版）
     */
    static void decode_segment_lut(
        std::span<const uint8_t> encoded,
        const Checkpoint& cp,
        uint32_t num_tokens,
        const SIMDDecodeTable& tbl,
        int* output)
    {
        std::array<uint32_t, 8> states = cp.states;
        const uint8_t* ptr;
        
        if (cp.byte_offset == 0) {
            ptr = encoded.data() + 32;
        } else {
            ptr = encoded.data() + cp.byte_offset;
        }
        
        for (uint32_t i = 0; i < num_tokens; ++i) {
            int lane = i % 8;
            
            uint32_t slot = states[lane] & (RANS_TOTAL - 1);
            uint32_t symbol = tbl.slot_to_symbol[slot];
            uint32_t freq = tbl.freq[symbol];
            uint32_t bias = tbl.bias[symbol];
            
            states[lane] = (states[lane] >> RANS_LOG2_TOTAL) * freq + slot - bias;
            
            while (states[lane] < RANS_LOWER_BOUND) {
                states[lane] = (states[lane] << 8) | *ptr++;
            }
            
            output[i] = static_cast<int>(symbol);
        }
    }
};

} // namespace hakonyans
