#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/pindex.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace hakonyans {

/**
 * Grayscale image encoder
 * 
 * Phase 5: Grayscale only, single tile, scalar DCT
 * Phase 5.2: Color support
 * Phase 5.3: P-Index integration
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
        header.pindex_density = 2; // Default
        
        uint32_t pad_w = header.padded_width();
        uint32_t pad_h = header.padded_height();
        
        // 2. Build quant table
        uint16_t quant[64];
        QuantTable::build_quant_table(quality, quant);
        
        // 3. Encode plane (with P-Index for grayscale)
        auto tile_data = encode_plane(pixels, width, height, pad_w, pad_h, quant, true);
        
        // 4. Assemble .hkn file
        QMATChunk qmat;
        qmat.quality = quality;
        qmat.num_tables = 1;
        std::memcpy(qmat.quant_y, quant, 128);
        auto qmat_data = qmat.serialize();
        
        ChunkDirectory dir;
        dir.add("QMAT", 0, qmat_data.size());
        dir.add("TIL0", 0, tile_data.size());
        
        auto dir_data = dir.serialize();
        size_t qmat_offset = 48 + dir_data.size();
        size_t tile_offset = qmat_offset + qmat_data.size();
        
        dir.entries[0].offset = qmat_offset;
        dir.entries[1].offset = tile_offset;
        dir_data = dir.serialize();
        
        std::vector<uint8_t> output;
        output.resize(48);
        header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        
        return output;
    }

    /**
     * Encode color image to .hkn (YCbCr 4:4:4)
     */
    static std::vector<uint8_t> encode_color(
        const uint8_t* rgb_data,
        uint32_t width,
        uint32_t height,
        uint8_t quality = 75
    ) {
        // 1. RGB -> YCbCr conversion
        std::vector<uint8_t> y_plane(width * height);
        std::vector<uint8_t> cb_plane(width * height);
        std::vector<uint8_t> cr_plane(width * height);
        
        for (uint32_t i = 0; i < width * height; i++) {
            rgb_to_ycbcr(rgb_data[i*3], rgb_data[i*3+1], rgb_data[i*3+2],
                         y_plane[i], cb_plane[i], cr_plane[i]);
        }
        
        // 2. Setup header
        FileHeader header;
        header.width = width;
        header.height = height;
        header.bit_depth = 8;
        header.num_channels = 3;
        header.colorspace = 0;  // YCbCr
        header.subsampling = 0; // 4:4:4
        header.tile_cols = 1;
        header.tile_rows = 1;
        header.quality = quality;
        header.pindex_density = 2; // Default
        
        uint32_t pad_w = header.padded_width();
        uint32_t pad_h = header.padded_height();
        
        // 3. Build quant tables
        uint16_t quant[64];
        QuantTable::build_quant_table(quality, quant);
        
        // 4. Encode each plane
        auto tile_y  = encode_plane(y_plane.data(),  width, height, pad_w, pad_h, quant, true);
        auto tile_cb = encode_plane(cb_plane.data(), width, height, pad_w, pad_h, quant, true);
        auto tile_cr = encode_plane(cr_plane.data(), width, height, pad_w, pad_h, quant, true);
        
        // 5. Assemble .hkn file
        QMATChunk qmat;
        qmat.quality = quality;
        qmat.num_tables = 1;
        std::memcpy(qmat.quant_y, quant, 128);
        auto qmat_data = qmat.serialize();
        
        ChunkDirectory dir;
        dir.add("QMAT", 0, qmat_data.size());
        dir.add("TIL0", 0, tile_y.size());
        dir.add("TIL1", 0, tile_cb.size());
        dir.add("TIL2", 0, tile_cr.size());
        
        auto dir_data = dir.serialize();
        size_t qmat_offset = 48 + dir_data.size();
        size_t til0_offset = qmat_offset + qmat_data.size();
        size_t til1_offset = til0_offset + tile_y.size();
        size_t til2_offset = til1_offset + tile_cb.size();
        
        dir.entries[0].offset = qmat_offset;
        dir.entries[1].offset = til0_offset;
        dir.entries[2].offset = til1_offset;
        dir.entries[3].offset = til2_offset;
        dir_data = dir.serialize();
        
        std::vector<uint8_t> output;
        output.resize(48);
        header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_y.begin(), tile_y.end());
        output.insert(output.end(), tile_cb.begin(), tile_cb.end());
        output.insert(output.end(), tile_cr.begin(), tile_cr.end());
        
        return output;
    }

private:
    /**
     * Helper to encode a single plane into a TILE chunk payload
     */
    static std::vector<uint8_t> encode_plane(
        const uint8_t* pixels,
        uint32_t width, uint32_t height,
        uint32_t pad_w, uint32_t pad_h,
        const uint16_t quant[64],
        bool generate_pindex = false
    ) {
        // Pad
        std::vector<uint8_t> padded = pad_image(pixels, width, height, pad_w, pad_h);
        
        // Process blocks
        int num_blocks_x = pad_w / 8;
        int num_blocks_y = pad_h / 8;
        int total_blocks = num_blocks_x * num_blocks_y;
        
        std::vector<Token> dc_tokens;
        std::vector<Token> ac_tokens;
        dc_tokens.reserve(total_blocks);
        
        int16_t prev_dc = 0;
        for (int by = 0; by < num_blocks_y; by++) {
            for (int bx = 0; bx < num_blocks_x; bx++) {
                int16_t block[64];
                extract_block(padded.data(), pad_w, pad_h, bx, by, block);
                
                int16_t dct_out[64];
                DCT::forward(block, dct_out);
                
                int16_t zigzag[64];
                Zigzag::scan(dct_out, zigzag);
                
                int16_t quantized[64];
                QuantTable::quantize(zigzag, quant, quantized);
                
                int16_t dc_diff = quantized[0] - prev_dc;
                prev_dc = quantized[0];
                dc_tokens.push_back(Tokenizer::tokenize_dc(dc_diff));
                
                auto ac_tok = Tokenizer::tokenize_ac(&quantized[1]);
                ac_tokens.insert(ac_tokens.end(), ac_tok.begin(), ac_tok.end());
            }
        }
        
        // Encode tokens
        CDFTable cdf_dc = build_cdf(dc_tokens);
        CDFTable cdf_ac = build_cdf(ac_tokens);
        
        std::vector<uint8_t> pindex_data;
        auto dc_stream = encode_tokens(dc_tokens, cdf_dc);
        auto ac_stream = encode_tokens(ac_tokens, cdf_ac, generate_pindex ? &pindex_data : nullptr);
        
        // TILE chunk payload: dc_size(4) + ac_size(4) + pindex_size(4) + dc_stream + ac_stream + pindex_data
        std::vector<uint8_t> tile_data;
        uint32_t dc_size = dc_stream.size();
        uint32_t ac_size = ac_stream.size();
        uint32_t pindex_size = pindex_data.size();
        
        tile_data.resize(12);
        std::memcpy(&tile_data[0], &dc_size, 4);
        std::memcpy(&tile_data[4], &ac_size, 4);
        std::memcpy(&tile_data[8], &pindex_size, 4);
        tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end());
        tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
        if (pindex_size > 0) {
            tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
        }
        
        return tile_data;
    }

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
    static constexpr int ALPHABET_SIZE = 76;
    
    static CDFTable build_cdf(const std::vector<Token>& tokens) {
        std::vector<uint32_t> freq(ALPHABET_SIZE, 1);
        for (const auto& tok : tokens) {
            int sym = static_cast<int>(tok.type);
            if (sym < ALPHABET_SIZE) freq[sym]++;
        }
        CDFBuilder builder;
        return builder.build_from_freq(freq);
    }
    
    /**
     * Encode tokens with flat interleaved rANS
     * Returns: CDF_size(4) + CDF_data + token_count(4) + rANS_data_size(4) + rANS_data + raw_count(4) + raw_data
     */
    static std::vector<uint8_t> encode_tokens(
        const std::vector<Token>& tokens,
        const CDFTable& cdf,
        std::vector<uint8_t>* out_pindex = nullptr
    ) {
        std::vector<uint8_t> output;
        
        // 1. Write CDF
        int alpha = cdf.alphabet_size;
        std::vector<uint8_t> cdf_data(alpha * 4);
        for (int i = 0; i < alpha; i++) {
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
        
        // Generate P-Index if requested
        if (out_pindex) {
            auto pindex = PIndexBuilder::build(rans_buffer, cdf, tokens.size(), 1024);
            *out_pindex = PIndexCodec::serialize(pindex);
        }
        
        // Write rANS data size + data
        uint32_t rans_size = rans_buffer.size();
        size_t rans_size_offset = output.size();
        output.resize(rans_size_offset + 4);
        std::memcpy(&output[rans_size_offset], &rans_size, 4);
        output.insert(output.end(), rans_buffer.begin(), rans_buffer.end());
        
        // 4. Write raw bits
        std::vector<uint8_t> raw_data;
        uint32_t raw_count = 0;
        for (const auto& tok : tokens) {
            if (tok.raw_bits_count > 0) {
                raw_data.push_back(tok.raw_bits_count);
                raw_data.push_back(tok.raw_bits & 0xFF);
                raw_data.push_back((tok.raw_bits >> 8) & 0xFF);
                raw_count++;
            }
        }
        size_t raw_count_offset = output.size();
        output.resize(raw_count_offset + 4);
        std::memcpy(&output[raw_count_offset], &raw_count, 4);
        output.insert(output.end(), raw_data.begin(), raw_data.end());
        
        return output;
    }
};

} // namespace hakonyans