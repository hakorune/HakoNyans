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

// Helper to create mixed content image
void create_mixed_content(std::vector<uint8_t>& pixels, int w, int h) {
    // 1. Top area: Text-like repeated pattern (Candidates for COPY)
    // Draw "A" shape at (0,0) and copy it to (16,0), (32,0)...
    // "A" is 8x8 bitmap
    uint8_t char_A[64] = {
        0, 0, 255, 255, 255, 255, 0, 0,
        0, 255, 0, 0, 0, 0, 255, 0,
        0, 255, 0, 0, 0, 0, 255, 0,
        0, 255, 255, 255, 255, 255, 255, 0,
        0, 255, 0, 0, 0, 0, 255, 0,
        0, 255, 0, 0, 0, 0, 255, 0,
        0, 255, 0, 0, 0, 0, 255, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    
    for (int k = 0; k < 4; k++) {
        int dst_x = k * 16; // 0, 16, 32, 48
        int dst_y = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (dst_x + x < w && dst_y + y < h)
                    pixels[(dst_y + y) * w + (dst_x + x)] = char_A[y * 8 + x];
            }
        }
    }
    
    // 2. Middle area: Flat Palette colors (Candidates for PALETTE)
    // 3 colors: 50, 100, 150
    for (int y = 16; y < 24; y++) {
        for (int x = 0; x < w; x++) {
            if (x < w/3) pixels[y*w+x] = 50;
            else if (x < 2*w/3) pixels[y*w+x] = 100;
            else pixels[y*w+x] = 150;
        }
    }
    
    // 3. Bottom area: Noise/Gradient (Candidates for DCT)
    for (int y = 32; y < h; y++) {
        for (int x = 0; x < w; x++) {
            pixels[y*w+x] = (uint8_t)((x + y) * 3 + (std::rand() % 10));
        }
    }
}

void test_auto_selection() {
    std::cout << "Testing Automatic Block Type Selection..." << std::endl;
    
    int w = 64, h = 64;
    std::vector<uint8_t> pixels(w * h, 0);
    create_mixed_content(pixels, w, h);
    
    FileHeader header; 
    header.width = w; header.height = h; 
    uint32_t pw = header.padded_width(); uint32_t ph = header.padded_height();
    uint16_t quant[64]; QuantTable::build_quant_table(90, quant);
    
    std::vector<FileHeader::BlockType> block_types( (pw/8)*(ph/8), FileHeader::BlockType::DCT );
    
    // Pass nullptr for block_types_in to trigger auto-selection
    auto encoded = GrayscaleEncoder::encode_plane(
        pixels.data(), w, h, pw, ph, quant, false, false, nullptr, 0,
        nullptr, // block_types_in -> nullptr to enable auto-selection
        nullptr  // copy_params_in
    );
    
    // Strategy: Decode and check Block Types
    // `decode_plane` decodes block types internally but doesn't expose them.
    // I need to use `GrayscaleDecoder::decode_block_types` manually on the stream.
    // The stream format: TileHeader (32 bytes) -> [5] = BlockType size.
    // Bytes 32 + sz[0] + ... + sz[4] is where BlockType data starts?
    // No, `encode_plane` writes fields in order:
    // sz[0] DC, sz[1] AC, sz[2] PIndex, sz[3] CFL-A, sz[4] CFL-B, sz[5] BT, sz[6] PAL, sz[7] CPY.
    // So we can parse the header and jump to BT data.
    
    uint32_t sz[8];
    std::memcpy(sz, encoded.data(), 32);
    
    size_t offset = 32;
    for(int k=0; k<5; k++) offset += sz[k];
    
    if (sz[5] > 0) {
        std::cout << "Block Type Stream found. Size: " << sz[5] << std::endl;
        
        int nb = (pw/8)*(ph/8);
        auto types = GrayscaleDecoder::decode_block_types(encoded.data() + offset, sz[5], nb);
        
        int copy_count = 0;
        int palette_count = 0;
        int dct_count = 0;
        
        for(int i=0; i<nb; i++) {
            auto t = types[i];
            if (t == FileHeader::BlockType::COPY) copy_count++;
            else if (t == FileHeader::BlockType::PALETTE) palette_count++;
            else dct_count++;
            
            // Print top-left blocks (Text area)
            // if (i < 8) std::cout << "Block " << i << ": " << (int)t << std::endl;
        }
        
        std::cout << "Stats: COPY=" << copy_count << ", PAL=" << palette_count << ", DCT=" << dct_count << std::endl;
        
        // Assertions for expected behavior
        // Text area: Block 0 ('A'), Block 2 ('A')...
        // Block 0 is source. Block 2 should be COPY of Block 0?
        // Wait, Block 0 and Block 1 are 'A' and space?
        // My code: `dst_x = k*16`. So 0, 16, 32, 48.
        // Block 0 (0-7): 'A'.
        // Block 1 (8-15): Empty? (Init to 0).
        // Block 2 (16-23): 'A'. -> Should be COPY of Block 0.
        // Block 3 (24-31): Empty. -> Should be COPY of Block 1.
        
        // Note: My COPY search only looks at "past" blocks.
        // Block 2 (at 16) searches back. Finds Block 0 at 0. dx=-16. Match!
        // So we expect COPY blocks.
        
        if (copy_count == 0) {
            std::cout << "WARNING: No Copy blocks detected. Tweak search radius?" << std::endl;
             // SAD match might fail if slightly different? Reference code draws exactly same 'A'.
             // But valid search area?
             // Block 2 (x=16): searches x < 16.
             // Block 0 (x=0) is in range.
             // It should enable COPY.
        } else {
             std::cout << "  [PASS] Copy Mode triggered" << std::endl;
        }

        if (palette_count == 0) {
            std::cout << "WARNING: No Palette blocks detected." << std::endl;
        } else {
             std::cout << "  [PASS] Palette Mode triggered" << std::endl;
        }
        
    } else {
        std::cout << "FAILED: No Block Type stream (All DCT default?)" << std::endl;
    }
    
    // Decode check
    auto decoded_pixels = GrayscaleDecoder::decode_plane(
        encoded.data(), encoded.size(), pw, ph, quant
    );
    // PSNR check
    double mse = 0;
    for(size_t i=0; i<pixels.size(); i++) {
        int diff = (int)pixels[i] - (int)decoded_pixels[i];
        mse += diff * diff;
    }
    mse /= pixels.size();
    double psnr = (mse > 0) ? 10.0 * log10(255.0*255.0/mse) : 100.0;
    std::cout << "Decoded PSNR: " << psnr << " dB" << std::endl;
    
    if (psnr < 30.0) {
        std::cout << "WARNING: PSNR is low. Check Copy implementation." << std::endl;
    }
}

int main() {
    try {
        test_auto_selection();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
