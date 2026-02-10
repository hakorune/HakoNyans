#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/parallel_decode.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <future>
#include <thread>

namespace hakonyans {

/**
 * Grayscale image decoder
 */
class GrayscaleDecoder {
public:
    /**
     * Decode .hkn to grayscale image
     */
    static std::vector<uint8_t> decode(const std::vector<uint8_t>& hkn_data) {
        if (hkn_data.size() < 48) throw std::runtime_error("File too small");
        FileHeader header = FileHeader::read(hkn_data.data());
        if (!header.is_valid()) throw std::runtime_error("Invalid header");
        
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn_data[48], hkn_data.size() - 48);
        const ChunkEntry* qmat_entry = dir.find("QMAT");
        if (!qmat_entry) throw std::runtime_error("QMAT chunk not found");
        QMATChunk qmat = QMATChunk::deserialize(&hkn_data[qmat_entry->offset], qmat_entry->size);
        
        uint16_t deq[64];
        std::memcpy(deq, qmat.quant_y, 128);
        
        const ChunkEntry* tile_entry = dir.find("TIL0");
        if (!tile_entry) tile_entry = dir.find("TILE");
        if (!tile_entry) throw std::runtime_error("TIL0 chunk not found");
        
        uint32_t pad_w = header.padded_width();
        uint32_t pad_h = header.padded_height();
        auto padded = decode_plane(&hkn_data[tile_entry->offset], tile_entry->size, pad_w, pad_h, deq);
        
        std::vector<uint8_t> output(header.width * header.height);
        for (uint32_t y = 0; y < header.height; y++) {
            std::memcpy(&output[y * header.width], &padded[y * pad_w], header.width);
        }
        return output;
    }

    /**
     * Decode .hkn to color image (RGB)
     */
    static std::vector<uint8_t> decode_color(
        const std::vector<uint8_t>& hkn_data,
        int& out_width,
        int& out_height
    ) {
        if (hkn_data.size() < 48) throw std::runtime_error("File too small");
        FileHeader header = FileHeader::read(hkn_data.data());
        if (!header.is_valid()) throw std::runtime_error("Invalid header");
        
        out_width = header.width;
        out_height = header.height;
        
        ChunkDirectory dir = ChunkDirectory::deserialize(&hkn_data[48], hkn_data.size() - 48);
        const ChunkEntry* qmat_entry = dir.find("QMAT");
        if (!qmat_entry) throw std::runtime_error("QMAT chunk not found");
        QMATChunk qmat = QMATChunk::deserialize(&hkn_data[qmat_entry->offset], qmat_entry->size);
        
        uint16_t deq[64];
        std::memcpy(deq, qmat.quant_y, 128);
        
        uint32_t pad_w = header.padded_width();
        uint32_t pad_h = header.padded_height();
        
        const ChunkEntry* til0 = dir.find("TIL0");
        const ChunkEntry* til1 = dir.find("TIL1");
        const ChunkEntry* til2 = dir.find("TIL2");
        if (!til0 || !til1 || !til2) throw std::runtime_error("Missing color tile chunks");
        
        // Decode planes in parallel
        auto f0 = std::async(std::launch::async, [&]() { return decode_plane(&hkn_data[til0->offset], til0->size, pad_w, pad_h, deq); });
        auto f1 = std::async(std::launch::async, [&]() { return decode_plane(&hkn_data[til1->offset], til1->size, pad_w, pad_h, deq); });
        auto f2 = std::async(std::launch::async, [&]() { return decode_plane(&hkn_data[til2->offset], til2->size, pad_w, pad_h, deq); });
        
        auto y_padded  = f0.get();
        auto cb_padded = f1.get();
        auto cr_padded = f2.get();
        
        std::vector<uint8_t> rgb(header.width * header.height * 3);
        unsigned int num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
        
        std::vector<std::future<void>> futures;
        int rows_per_thread = header.height / num_threads;
        for (unsigned int t = 0; t < num_threads; t++) {
            int start_y = t * rows_per_thread;
            int end_y = (t == num_threads - 1) ? header.height : (t + 1) * rows_per_thread;
            futures.push_back(std::async(std::launch::async, [=, &y_padded, &cb_padded, &cr_padded, &rgb]() {
                for (int y = start_y; y < end_y; y++) {
                    for (uint32_t x = 0; x < header.width; x++) {
                        uint32_t src_idx = y * pad_w + x;
                        uint32_t dst_idx = (y * header.width + x) * 3;
                        ycbcr_to_rgb(y_padded[src_idx], cb_padded[src_idx], cr_padded[src_idx],
                                     rgb[dst_idx], rgb[dst_idx+1], rgb[dst_idx+2]);
                    }
                }
            }));
        }
        for (auto& f : futures) f.get();
        return rgb;
    }

private:
    static std::vector<uint8_t> decode_plane(
        const uint8_t* tile_data, size_t tile_size,
        uint32_t pad_w, uint32_t pad_h,
        const uint16_t deq[64]
    ) {
        uint32_t dc_size, ac_size, pindex_size;
        std::memcpy(&dc_size, &tile_data[0], 4);
        std::memcpy(&ac_size, &tile_data[4], 4);
        std::memcpy(&pindex_size, &tile_data[8], 4);
        
        const uint8_t* dc_stream_ptr = &tile_data[12];
        const uint8_t* ac_stream_ptr = &tile_data[12 + dc_size];
        const uint8_t* pindex_ptr = (pindex_size > 0) ? &tile_data[12 + dc_size + ac_size] : nullptr;
        
        auto dc_tokens = decode_stream(dc_stream_ptr, dc_size);
        std::vector<Token> ac_tokens;
        if (pindex_ptr) {
            PIndex pindex = PIndexCodec::deserialize(std::span<const uint8_t>(pindex_ptr, pindex_size));
            ac_tokens = decode_stream_parallel(ac_stream_ptr, ac_size, pindex);
        } else {
            ac_tokens = decode_stream(ac_stream_ptr, ac_size);
        }
        
        int num_blocks_x = pad_w / 8;
        int num_blocks_y = pad_h / 8;
        std::vector<uint8_t> padded(pad_w * pad_h);
        
        // Block boundaries are expensive to find from token streams without more indices,
        // so we'll first find block boundaries in ac_tokens to allow parallel processing.
        std::vector<size_t> block_ac_starts;
        block_ac_starts.reserve(num_blocks_x * num_blocks_y);
        size_t current_ac = 0;
        for (int i = 0; i < num_blocks_x * num_blocks_y; i++) {
            block_ac_starts.push_back(current_ac);
            while (current_ac < ac_tokens.size()) {
                Token tok = ac_tokens[current_ac++];
                if (static_cast<int>(tok.type) == 63) break; // EOB
                if (static_cast<int>(tok.type) < 64) {
                    if (current_ac < ac_tokens.size()) current_ac++; // Skip MAGC
                }
            }
        }

        unsigned int num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
        std::vector<std::future<void>> futures;
        int blocks_per_thread = (num_blocks_x * num_blocks_y) / num_threads;
        
        for (unsigned int t = 0; t < num_threads; t++) {
            int start_block = t * blocks_per_thread;
            int end_block = (t == num_threads - 1) ? (num_blocks_x * num_blocks_y) : (t + 1) * blocks_per_thread;
            
            futures.push_back(std::async(std::launch::async, [=, &dc_tokens, &ac_tokens, &block_ac_starts, &padded, deq]() {
                // We need to re-calculate prev_dc for each thread segment
                int16_t current_prev_dc = 0;
                for (int i = 0; i < start_block; i++) {
                    current_prev_dc += Tokenizer::detokenize_dc(dc_tokens[i]);
                }
                
                for (int i = start_block; i < end_block; i++) {
                    int bx = i % num_blocks_x;
                    int by = i / num_blocks_x;
                    
                    int16_t dc_diff = Tokenizer::detokenize_dc(dc_tokens[i]);
                    int16_t dc = current_prev_dc + dc_diff;
                    current_prev_dc = dc;
                    
                    int16_t ac[63];
                    size_t ac_start = block_ac_starts[i];
                    size_t ac_end = (i == (num_blocks_x * num_blocks_y - 1)) ? ac_tokens.size() : block_ac_starts[i+1];
                    
                    std::vector<Token> block_ac_toks;
                    for (size_t k = ac_start; k < ac_end; k++) {
                        block_ac_toks.push_back(ac_tokens[k]);
                    }
                    Tokenizer::detokenize_ac(block_ac_toks, ac);
                    
                    int16_t quantized[64];
                    quantized[0] = dc;
                    std::memcpy(&quantized[1], ac, 63 * sizeof(int16_t));
                    
                    int16_t dequantized[64];
                    QuantTable::dequantize(quantized, deq, dequantized);
                    int16_t coeffs[64];
                    Zigzag::inverse_scan(dequantized, coeffs);
                    int16_t block[64];
                    DCT::inverse(coeffs, block);
                    
                    for (int y = 0; y < 8; y++) {
                        for (int x = 0; x < 8; x++) {
                            int16_t val = block[y * 8 + x] + 128;
                            val = (val < 0) ? 0 : (val > 255) ? 255 : val;
                            padded[(by * 8 + y) * pad_w + (bx * 8 + x)] = static_cast<uint8_t>(val);
                        }
                    }
                }
            }));
        }
        for (auto& f : futures) f.get();
        return padded;
    }

    static std::vector<Token> decode_stream(const uint8_t* stream, size_t size) {
        uint32_t cdf_size;
        std::memcpy(&cdf_size, &stream[0], 4);
        std::vector<uint32_t> freq_vec(cdf_size / 4);
        std::memcpy(freq_vec.data(), &stream[4], cdf_size);
        CDFBuilder builder;
        CDFTable cdf = builder.build_from_freq(freq_vec);
        uint32_t token_count;
        std::memcpy(&token_count, &stream[4 + cdf_size], 4);
        uint32_t rans_size;
        std::memcpy(&rans_size, &stream[8 + cdf_size], 4);
        FlatInterleavedDecoder decoder(std::span<const uint8_t>(&stream[12 + cdf_size], rans_size));
        std::vector<Token> tokens;
        tokens.reserve(token_count);
        for (uint32_t i = 0; i < token_count; i++) {
            tokens.emplace_back(static_cast<TokenType>(decoder.decode_symbol(cdf)), 0, 0);
        }
        uint32_t raw_count;
        size_t offset = 12 + cdf_size + rans_size;
        std::memcpy(&raw_count, &stream[offset], 4);
        offset += 4;
        size_t raw_idx = 0;
        for (auto& tok : tokens) {
            if (tok.type >= TokenType::MAGC_0 && tok.type <= TokenType::MAGC_11) {
                int magc = static_cast<int>(tok.type) - static_cast<int>(TokenType::MAGC_0);
                if (magc > 0 && raw_idx < raw_count) {
                    tok.raw_bits_count = stream[offset];
                    tok.raw_bits = stream[offset + 1] | (stream[offset + 2] << 8);
                    offset += 3;
                    raw_idx++;
                }
            }
        }
        return tokens;
    }

    static std::vector<Token> decode_stream_parallel(const uint8_t* stream, size_t size, const PIndex& pindex) {
        uint32_t cdf_size;
        std::memcpy(&cdf_size, &stream[0], 4);
        std::vector<uint32_t> freq_vec(cdf_size / 4);
        std::memcpy(freq_vec.data(), &stream[4], cdf_size);
        CDFBuilder builder;
        CDFTable cdf = builder.build_from_freq(freq_vec);
        uint32_t token_count;
        std::memcpy(&token_count, &stream[4 + cdf_size], 4);
        uint32_t rans_size;
        std::memcpy(&rans_size, &stream[8 + cdf_size], 4);
        auto symbols = ParallelDecoder::decode(std::span<const uint8_t>(&stream[12 + cdf_size], rans_size), pindex, cdf, std::thread::hardware_concurrency());
        std::vector<Token> tokens;
        tokens.reserve(token_count);
        for (int sym : symbols) tokens.emplace_back(static_cast<TokenType>(sym), 0, 0);
        uint32_t raw_count;
        size_t offset = 12 + cdf_size + rans_size;
        std::memcpy(&raw_count, &stream[offset], 4);
        offset += 4;
        size_t raw_idx = 0;
        for (auto& tok : tokens) {
            if (tok.type >= TokenType::MAGC_0 && tok.type <= TokenType::MAGC_11) {
                int magc = static_cast<int>(tok.type) - static_cast<int>(TokenType::MAGC_0);
                if (magc > 0 && raw_idx < raw_count) {
                    tok.raw_bits_count = stream[offset];
                    tok.raw_bits = stream[offset + 1] | (stream[offset + 2] << 8);
                    offset += 3;
                    raw_idx++;
                }
            }
        }
        return tokens;
    }
};

} // namespace hakonyans