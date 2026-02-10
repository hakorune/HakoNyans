#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include <vector>
#include <cstring>
#include <stdexcept>

namespace hakonyans {

/**
 * Grayscale image decoder
 */
class GrayscaleDecoder {
public:
    /**
     * Decode .hkn to grayscale image
     * @param hkn_data .hkn file bytes
     * @return Decoded pixels (8-bit grayscale, row-major)
     */
    static std::vector<uint8_t> decode(const std::vector<uint8_t>& hkn_data) {
        if (hkn_data.size() < 48) {
            throw std::runtime_error("File too small");
        }
        
        // 1. Parse FileHeader
        FileHeader header = FileHeader::read(hkn_data.data());
        if (!header.is_valid()) {
            throw std::runtime_error("Invalid header");
        }
        
        // 2. Parse ChunkDirectory
        size_t dir_offset = 48;
        ChunkDirectory dir = ChunkDirectory::deserialize(
            &hkn_data[dir_offset],
            hkn_data.size() - dir_offset
        );
        
        // 3. Load QMAT
        const ChunkEntry* qmat_entry = dir.find("QMAT");
        if (!qmat_entry) {
            throw std::runtime_error("QMAT chunk not found");
        }
        
        QMATChunk qmat = QMATChunk::deserialize(
            &hkn_data[qmat_entry->offset],
            qmat_entry->size
        );
        
        uint16_t deq[64];
        std::memcpy(deq, qmat.quant_y, 128);
        
        // 4. Load TILE
        const ChunkEntry* tile_entry = dir.find("TILE");
        if (!tile_entry) {
            throw std::runtime_error("TILE chunk not found");
        }
        
        const uint8_t* tile_data = &hkn_data[tile_entry->offset];
        
        uint32_t dc_size, ac_size, pindex_size;
        std::memcpy(&dc_size, &tile_data[0], 4);
        std::memcpy(&ac_size, &tile_data[4], 4);
        std::memcpy(&pindex_size, &tile_data[8], 4);
        
        const uint8_t* dc_stream = &tile_data[12];
        const uint8_t* ac_stream = &tile_data[12 + dc_size];
        
        // 5. Decode DC stream
        auto dc_tokens = decode_stream(dc_stream, dc_size);
        
        // 6. Decode AC stream
        auto ac_tokens = decode_stream(ac_stream, ac_size);
        
        // 7. Reconstruct blocks
        uint32_t pad_w = header.padded_width();
        uint32_t pad_h = header.padded_height();
        int num_blocks_x = pad_w / 8;
        int num_blocks_y = pad_h / 8;
        
        std::vector<uint8_t> padded(pad_w * pad_h);
        
        size_t dc_idx = 0;
        size_t ac_idx = 0;
        int16_t prev_dc = 0;
        
        for (int by = 0; by < num_blocks_y; by++) {
            for (int bx = 0; bx < num_blocks_x; bx++) {
                // DC DPCM restore
                if (dc_idx >= dc_tokens.size()) {
                    throw std::runtime_error("DC tokens exhausted");
                }
                int16_t dc_diff = Tokenizer::detokenize_dc(dc_tokens[dc_idx++]);
                int16_t dc = prev_dc + dc_diff;
                prev_dc = dc;
                
                // AC detokenize
                int16_t ac[63];
                std::vector<Token> block_ac_tokens;
                
                // Collect AC tokens for this block (until EOB)
                while (ac_idx < ac_tokens.size()) {
                    Token tok = ac_tokens[ac_idx++];
                    block_ac_tokens.push_back(tok);
                    
                    // Check for EOB (ZRUN_63)
                    if (static_cast<int>(tok.type) == 63) {
                        break;
                    }
                    
                    // Skip MAGC token (comes after ZRUN)
                    if (static_cast<int>(tok.type) < 64) {
                        if (ac_idx < ac_tokens.size()) {
                            block_ac_tokens.push_back(ac_tokens[ac_idx++]);
                        }
                    }
                }
                
                Tokenizer::detokenize_ac(block_ac_tokens, ac);
                
                // Combine DC + AC
                int16_t quantized[64];
                quantized[0] = dc;
                std::memcpy(&quantized[1], ac, 63 * sizeof(int16_t));
                
                // Dequantize
                int16_t dequantized[64];
                QuantTable::dequantize(quantized, deq, dequantized);
                
                // Inverse zigzag
                int16_t coeffs[64];
                Zigzag::inverse_scan(dequantized, coeffs);
                
                // IDCT
                int16_t block[64];
                DCT::inverse(coeffs, block);
                
                // Store block (clamp to [0, 255])
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        int16_t val = block[y * 8 + x] + 128;  // Uncenter
                        val = (val < 0) ? 0 : (val > 255) ? 255 : val;
                        padded[(by * 8 + y) * pad_w + (bx * 8 + x)] = static_cast<uint8_t>(val);
                    }
                }
            }
        }
        
        // 8. Crop to original size
        std::vector<uint8_t> output(header.width * header.height);
        for (uint32_t y = 0; y < header.height; y++) {
            std::memcpy(
                &output[y * header.width],
                &padded[y * pad_w],
                header.width
            );
        }
        
        return output;
    }

private:
    /**
     * Decode rANS stream to tokens
     * Stream format: CDF_size(4) + CDF_data + token_count(4) + rANS_data_size(4) + rANS_data + raw_count(4) + raw_data
     */
    static std::vector<Token> decode_stream(const uint8_t* stream, size_t size) {
        if (size < 8) {
            throw std::runtime_error("Stream too small");
        }
        
        size_t offset = 0;
        
        // 1. Read CDF
        uint32_t cdf_size;
        std::memcpy(&cdf_size, &stream[offset], 4);
        offset += 4;
        
        if (offset + cdf_size > size) {
            throw std::runtime_error("CDF data truncated");
        }
        
        // Derive alphabet size from CDF data size
        int alpha = cdf_size / 4;
        std::vector<uint32_t> freq_vec(alpha);
        for (int i = 0; i < alpha; i++) {
            std::memcpy(&freq_vec[i], &stream[offset + i * 4], 4);
        }
        offset += cdf_size;
        
        CDFBuilder builder;
        CDFTable cdf = builder.build_from_freq(freq_vec);
        
        // 2. Read token count
        if (offset + 4 > size) {
            throw std::runtime_error("Token count missing");
        }
        
        uint32_t token_count;
        std::memcpy(&token_count, &stream[offset], 4);
        offset += 4;
        
        // 3. Read rANS data size
        if (offset + 4 > size) {
            throw std::runtime_error("rANS size missing");
        }
        
        uint32_t rans_size;
        std::memcpy(&rans_size, &stream[offset], 4);
        offset += 4;
        
        if (offset + rans_size > size) {
            throw std::runtime_error("rANS data truncated");
        }
        
        // 4. Decode rANS symbols
        std::span<const uint8_t> data_span(&stream[offset], rans_size);
        FlatInterleavedDecoder decoder(data_span);
        
        std::vector<Token> tokens;
        tokens.reserve(token_count);
        
        for (uint32_t i = 0; i < token_count; i++) {
            uint8_t symbol = decoder.decode_symbol(cdf);
            tokens.emplace_back(static_cast<TokenType>(symbol), 0, 0);
        }
        
        offset += rans_size;
        
        // 5. Read raw bits
        if (offset + 4 > size) {
            throw std::runtime_error("Raw count missing");
        }
        
        uint32_t raw_count;
        std::memcpy(&raw_count, &stream[offset], 4);
        offset += 4;
        
        // Assign raw bits to tokens that need them
        size_t raw_idx = 0;
        for (auto& tok : tokens) {
            if (tok.type >= TokenType::MAGC_0 && tok.type <= TokenType::MAGC_11) {
                int magc = static_cast<int>(tok.type) - static_cast<int>(TokenType::MAGC_0);
                if (magc > 0 && raw_idx < raw_count) {
                    // Read raw entry: count(1) + bits_lo(1) + bits_hi(1)
                    if (offset + 3 > size) break;
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
