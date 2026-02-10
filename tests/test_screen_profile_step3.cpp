#include <iostream>
#include <vector>
#include <cassert>
#include <random>
#include <cstring>
#include <cmath>
#include <algorithm>

#include "../src/codec/headers.h"
#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include "../src/codec/copy.h"

using namespace hakonyans;

void test_copy_codec_stream() {
    std::cout << "Testing CopyCodec Stream..." << std::endl;
    
    std::vector<CopyParams> input;
    input.emplace_back(-8, 0); // Left 8px
    input.emplace_back(0, -8); // Top 8px
    input.emplace_back(-16, -16);
    input.emplace_back(10, 5); // Positive (valid if pointing to prev decoded)
    
    auto encoded = CopyCodec::encode_copy_stream(input);
    
    std::vector<CopyParams> decoded;
    CopyCodec::decode_copy_stream(encoded.data(), encoded.size(), decoded, input.size());
    
    assert(decoded.size() == input.size());
    for(size_t i=0; i<input.size(); i++) {
        assert(decoded[i] == input[i]);
    }
    std::cout << "  [PASS] Stream Encode/Decode" << std::endl;
}

void test_copy_execution() {
    std::cout << "Testing Copy Block Execution..." << std::endl;
    
    // Create an image with a pattern
    int w = 64, h = 64;
    std::vector<uint8_t> pixels(w * h, 0);
    
    // Fill top-left 8x8 with a pattern (Gradient)
    for(int y=0; y<8; y++) {
        for(int x=0; x<8; x++) {
            pixels[y*w+x] = (uint8_t)((x + y) * 10);
        }
    }
    
    // Block 1 (x=8, y=0) should copy from (-8, 0) which is Block 0
    FileHeader header; 
    header.width = w; header.height = h; 
    uint32_t pw = header.padded_width(); uint32_t ph = header.padded_height();
    
    uint16_t quant[64]; QuantTable::build_quant_table(90, quant);
    
    int nb = (pw/8) * (ph/8);
    std::vector<FileHeader::BlockType> block_types(nb, FileHeader::BlockType::DCT);
    block_types[1] = FileHeader::BlockType::COPY;
    
    std::vector<CopyParams> copy_params;
    copy_params.emplace_back(-8, 0); // For Block 1
    
    // Encoder: encodes Block 0 as DCT (lossy), Block 1 as COPY command
    auto encoded = GrayscaleEncoder::encode_plane(
        pixels.data(), w, h, pw, ph, quant, false, false, nullptr, 0,
        &block_types, &copy_params
    );
    
    std::cout << "  Encoded size: " << encoded.size() << " bytes" << std::endl;
    
    // Decoder: Decodes Block 0 (approximate), and copies it to Block 1
    auto decoded_pixels = GrayscaleDecoder::decode_plane(
        encoded.data(), encoded.size(), pw, ph, quant
    );
    
    // Verify Block 0 (DCT) matches source
    for(int y=0; y<8; y++) {
        for(int x=0; x<8; x++) {
            int idx = y*pw+x;
            int diff = std::abs((int)decoded_pixels[idx] - (int)pixels[idx]);
            // DCT is lossy, allow small diff
             if (diff > 15) {
                 std::cout << "Mismatch at " << x << "," << y << ": " << (int)decoded_pixels[idx] << " vs " << (int)pixels[idx] << std::endl;
             }
             assert(diff <= 15);
        }
    }
    std::cout << "  [PASS] Block 0 (DCT) decoded correctly" << std::endl;
    
    // Verify Block 1 (COPY) matches Block 0
    for(int y=0; y<8; y++) {
        for(int x=0; x<8; x++) {
            int src_idx = (y+0)*pw + (x+0);
            int dst_idx = (y+0)*pw + (x+8);
            
            // Decoded Block 1 should be EXACT copy of Decoded Block 0
            if (decoded_pixels[dst_idx] != decoded_pixels[src_idx]) {
                std::cout << "Copy mismatch at local " << x << "," << y << ": " 
                          << (int)decoded_pixels[dst_idx] << " vs " << (int)decoded_pixels[src_idx] << std::endl;
            }
            assert(decoded_pixels[dst_idx] == decoded_pixels[src_idx]);
        }
    }
    std::cout << "  [PASS] Block 1 (COPY) matches Block 0" << std::endl;
}

int main() {
    try {
        test_copy_codec_stream();
        test_copy_execution();
        std::cout << "All Step 3 tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
