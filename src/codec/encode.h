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
 * Grayscale image encoder
 * 
 * Phase 5: Grayscale only, single tile, scalar DCT
 * Phase 5.1: Color support
 * Phase 5.2: SIMD DCT
 */
class GrayscaleEncoder {
public:
    /**
     * Encode grayscale image to .hkn
     * @param pixels Input image (row-major, 8-bit grayscale)
     * @param width Image width
     * @param height Image height
     * @param quality Quality 1..100
     * @return .hkn file bytes
     */
    static std::vector<uint8_t> encode(
        const uint8_t* pixels,
        uint32_t width,
        uint32_t height,
        uint8_t quality = 75
    ) {
        // 1. Setup header
        FileHeader header;
        header.width = width;
        header.height = height;
        header.bit_depth = 8;
        header.num_channels = 1;
        header.colorspace = 2;  // Grayscale
        header.subsampling = 0;
        header.tile_cols = 1;
        header.tile_rows = 1;
        header.quality = quality;
        
        // 2. Pad to 8x8 multiple
        uint32_t pad_w = header.padded_width();
        uint32_t pad_h = header.padded_height();
        std::vector<uint8_t> padded = pad_image(pixels, width, height, pad_w, pad_h);
        
        // 3. Build quant table
        uint16_t quant[64];
        QuantTable::build_quant_table(quality, quant);
        
        // 4. Process blocks
        int num_blocks_x = pad_w / 8;
        int num_blocks_y = pad_h / 8;
        int total_blocks = num_blocks_x * num_blocks_y;
        
        std::vector<Token> dc_tokens;
        std::vector<Token> ac_tokens;
        dc_tokens.reserve(total_blocks);
        
        int16_t prev_dc = 0;  // For DPCM
        
        for (int by = 0; by < num_blocks_y; by++) {
            for (int bx = 0; bx < num_blocks_x; bx++) {
                // Extract 8×8 block
                int16_t block[64];
                extract_block(padded.data(), pad_w, pad_h, bx, by, block);
                
                // DCT
                int16_t dct_out[64];
                DCT::forward(block, dct_out);
                
                // Zigzag scan
                int16_t zigzag[64];
                Zigzag::scan(dct_out, zigzag);
                
                // Quantize
                int16_t quantized[64];
                QuantTable::quantize(zigzag, quant, quantized);
                
                // DC DPCM
                int16_t dc_diff = quantized[0] - prev_dc;
                prev_dc = quantized[0];
                dc_tokens.push_back(Tokenizer::tokenize_dc(dc_diff));
                
                // AC tokenize
                auto ac_tok = Tokenizer::tokenize_ac(&quantized[1]);
                ac_tokens.insert(ac_tokens.end(), ac_tok.begin(), ac_tok.end());
            }
        }
        
        // 5. Build CDF tables
        CDFTable cdf_dc = build_cdf(dc_tokens);
        CDFTable cdf_ac = build_cdf(ac_tokens);
        
        // 6. Encode tokens with rANS
        auto dc_stream = encode_tokens(dc_tokens, cdf_dc);
        auto ac_stream = encode_tokens(ac_tokens, cdf_ac);
        
        // 7. Assemble .hkn file
        std::vector<uint8_t> output;
        
        // Prepare chunks first
        QMATChunk qmat;
        qmat.quality = quality;
        qmat.num_tables = 1;
        std::memcpy(qmat.quant_y, quant, 128);
        auto qmat_data = qmat.serialize();
        
        // TILE chunk: dc_size(4) + ac_size(4) + pindex_size(4) + dc_stream + ac_stream
        std::vector<uint8_t> tile_data;
        uint32_t dc_size = dc_stream.size();
        uint32_t ac_size = ac_stream.size();
        uint32_t pindex_size = 0;  // No P-Index for Phase 5 initial
        
        tile_data.resize(12);
        std::memcpy(&tile_data[0], &dc_size, 4);
        std::memcpy(&tile_data[4], &ac_size, 4);
        std::memcpy(&tile_data[8], &pindex_size, 4);
        tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
        tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
        
        // Build directory
        ChunkDirectory dir;
        dir.add("QMAT", 0, qmat_data.size());  // Offset will be updated later
        dir.add("TILE", 0, tile_data.size());  // Offset will be updated later
        
        auto dir_data = dir.serialize();
        
        // Calculate actual offsets
        size_t qmat_offset = 48 + dir_data.size();
        size_t tile_offset = qmat_offset + qmat_data.size();
        
        // Update directory with correct offsets
        dir.entries[0].offset = qmat_offset;
        dir.entries[1].offset = tile_offset;
        dir_data = dir.serialize();
        
        // FileHeader (48 bytes)
        output.resize(48);
        header.write(output.data());
        
        // Append directory
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        
        // Append QMAT
        output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        
        // Append TILE
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        
        return output;
    }

private:
    /**
     * Pad image to 8x8 multiple (mirror padding)
     */
    static std::vector<uint8_t> pad_image(
        const uint8_t* pixels,
        uint32_t width, uint32_t height,
        uint32_t pad_w, uint32_t pad_h
    ) {
        std::vector<uint8_t> padded(pad_w * pad_h);
        
        for (uint32_t y = 0; y < pad_h; y++) {
            for (uint32_t x = 0; x < pad_w; x++) {
                uint32_t src_x = (x < width) ? x : (width - 1);
                uint32_t src_y = (y < height) ? y : (height - 1);
                padded[y * pad_w + x] = pixels[src_y * width + src_x];
            }
        }
        
        return padded;
    }
    
    /**
     * Extract 8×8 block from image
     */
    static void extract_block(
        const uint8_t* pixels,
        uint32_t stride, uint32_t height,
        int bx, int by,
        int16_t block[64]
    ) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint8_t p = pixels[(by * 8 + y) * stride + (bx * 8 + x)];
                block[y * 8 + x] = static_cast<int16_t>(p) - 128;  // Center to [-128, 127]
            }
        }
    }
    
    /**
     * Build CDF from token frequency
     */
    static CDFTable build_cdf(const std::vector<Token>& tokens) {
        std::vector<uint32_t> freq(256, 1);  // Initialize with 1 for smoothing
        
        for (const auto& tok : tokens) {
            freq[static_cast<uint8_t>(tok.type)]++;
        }
        
        CDFBuilder builder;
        return builder.build_from_freq(freq);
    }
    
    /**
     * Encode tokens with flat interleaved rANS
     * Returns: CDF_size(4) + CDF_data + token_count(4) + rANS_data
     */
    static std::vector<uint8_t> encode_tokens(
        const std::vector<Token>& tokens,
        const CDFTable& cdf
    ) {
        std::vector<uint8_t> output;
        
        // 1. Write CDF (freq table)
        std::vector<uint8_t> cdf_data;
        cdf_data.resize(256 * 4);  // 256 frequencies × 4 bytes
        for (int i = 0; i < 256; i++) {
            uint32_t f = cdf.freq[i];
            std::memcpy(&cdf_data[i * 4], &f, 4);
        }
        
        uint32_t cdf_size = cdf_data.size();
        output.resize(4);
        std::memcpy(output.data(), &cdf_size, 4);
        output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        
        // 2. Write token count
        uint32_t token_count = tokens.size();
        size_t count_offset = output.size();
        output.resize(count_offset + 4);
        std::memcpy(&output[count_offset], &token_count, 4);
        
        // 3. Encode rANS symbols
        FlatInterleavedEncoder encoder;
        for (const auto& tok : tokens) {
            encoder.encode_symbol(cdf, static_cast<uint8_t>(tok.type));
        }
        
        auto rans_buffer = encoder.finish();
        output.insert(output.end(), rans_buffer.begin(), rans_buffer.end());
        
        // TODO: Append raw bits (SIGN + REM) after rANS data
        
        return output;
    }
};

} // namespace hakonyans
