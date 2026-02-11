#include <iostream>
#include <vector>
#include <cassert>
#include <random>
#include <cstring>
#include <cmath>

#include "../src/codec/headers.h"
#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;

void test_rle_roundtrip() {
    std::cout << "Testing RLE BlockType Roundtrip..." << std::endl;

    // Case 1: All DCT (default)
    {
        std::vector<FileHeader::BlockType> input(100, FileHeader::BlockType::DCT);
        auto encoded = GrayscaleEncoder::encode_block_types(input);
        auto decoded = GrayscaleDecoder::decode_block_types(encoded.data(), encoded.size(), 100);
        
        assert(encoded.size() == 2);
        assert(decoded.size() == 100);
        for(size_t i=0; i<100; i++) assert(decoded[i] == FileHeader::BlockType::DCT);
        std::cout << "  [PASS] All DCT (100 blocks)" << std::endl;
    }

    // Case 2: Mixed types
    {
        std::vector<FileHeader::BlockType> input;
        for(int i=0; i<10; i++) input.push_back(FileHeader::BlockType::DCT);
        for(int i=0; i<5; i++) input.push_back(FileHeader::BlockType::PALETTE);
        for(int i=0; i<20; i++) input.push_back(FileHeader::BlockType::COPY);
        input.push_back(FileHeader::BlockType::DCT);
        
        auto encoded = GrayscaleEncoder::encode_block_types(input);
        auto decoded = GrayscaleDecoder::decode_block_types(encoded.data(), encoded.size(), input.size());
        
        assert(decoded.size() == input.size());
        for(size_t i=0; i<input.size(); i++) assert(decoded[i] == input[i]);
        std::cout << "  [PASS] Mixed types (DCT/PAL/COPY)" << std::endl;
    }

    // Case 3: Random long runs
    {
        std::vector<FileHeader::BlockType> input;
        for(int i=0; i<70; i++) input.push_back(FileHeader::BlockType::DCT);
        
        auto encoded = GrayscaleEncoder::encode_block_types(input);
        assert(encoded.size() == 2);
        
        auto decoded = GrayscaleDecoder::decode_block_types(encoded.data(), encoded.size(), 70);
        assert(decoded.size() == 70);
        for(auto t : decoded) assert(t == FileHeader::BlockType::DCT);
        std::cout << "  [PASS] Long runs (>64)" << std::endl;
    }
}

void test_full_codec_compatibility() {
    std::cout << "Testing Full Codec Compatibility (Step 1)..." << std::endl;
    
    int w = 64, h = 64;
    std::vector<uint8_t> pixels(w * h);
    for(int y=0; y<h; y++) for(int x=0; x<w; x++) pixels[y*w+x] = (x + y) * 2;
    
    auto encoded = GrayscaleEncoder::encode(pixels.data(), w, h, 90);
    
    FileHeader hdr = FileHeader::read(encoded.data());
    assert(hdr.version == FileHeader::VERSION);
    std::cout << "  [PASS] Header version is 0x" << std::hex << hdr.version << std::dec << std::endl;
    
    auto decoded_pixels = GrayscaleDecoder::decode(encoded);
    
    assert(decoded_pixels.size() == w * h);
    
    double mse = 0;
    for(size_t i=0; i<pixels.size(); i++) {
        int diff = (int)pixels[i] - (int)decoded_pixels[i];
        mse += diff * diff;
    }
    mse /= pixels.size();
    double psnr = 10.0 * log10(255.0*255.0/mse);
    
    std::cout << "  [PASS] Decode successful, PSNR: " << psnr << " dB" << std::endl;
    assert(psnr > 30.0);
}

int main() {
    try {
        test_rle_roundtrip();
        test_full_codec_compatibility();
        std::cout << "All Step 1 tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
