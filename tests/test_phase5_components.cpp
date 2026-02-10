#include "../src/codec/zigzag.h"
#include "../src/codec/quant.h"
#include "../src/codec/transform_dct.h"
#include "../src/entropy/nyans_p/tokenization_v2.h"
#include "../src/codec/headers.h"
#include <iostream>
#include <cstring>

using namespace hakonyans;

void test_zigzag() {
    std::cout << "Testing Zigzag... ";
    
    int16_t block[64];
    for (int i = 0; i < 64; i++) {
        block[i] = i;
    }
    
    int16_t zigzag[64];
    Zigzag::scan(block, zigzag);
    
    int16_t restored[64];
    Zigzag::inverse_scan(zigzag, restored);
    
    bool ok = true;
    for (int i = 0; i < 64; i++) {
        if (block[i] != restored[i]) {
            ok = false;
            break;
        }
    }
    
    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
}

void test_dct() {
    std::cout << "Testing DCT... ";
    
    // Simple pattern
    int16_t input[64];
    for (int i = 0; i < 64; i++) {
        input[i] = (i % 8 == 0) ? 100 : 0;
    }
    
    int16_t dct_out[64];
    DCT::forward(input, dct_out);
    
    int16_t idct_out[64];
    DCT::inverse(dct_out, idct_out);
    
    // Check approximate reconstruction
    bool ok = true;
    for (int i = 0; i < 64; i++) {
        int diff = std::abs(input[i] - idct_out[i]);
        if (diff > 5) {  // Allow small error
            ok = false;
            std::cout << "\n  [" << i << "] input=" << input[i] << " output=" << idct_out[i] << " diff=" << diff;
        }
    }
    
    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
}

void test_quant() {
    std::cout << "Testing Quantization... ";
    
    uint16_t quant[64];
    QuantTable::build_quant_table(50, quant);
    
    // Check reasonable values
    bool ok = (quant[0] >= 1 && quant[0] <= 100);  // DC should be reasonable
    
    std::cout << (ok ? "PASS (Q[0]=" : "FAIL (Q[0]=") << quant[0] << ")" << std::endl;
}

void test_tokenize_dc() {
    std::cout << "Testing DC tokenization... ";
    
    // Test DC=0
    Token t0 = Tokenizer::tokenize_dc(0);
    int16_t dc0 = Tokenizer::detokenize_dc(t0);
    
    // Test DC=42
    Token t42 = Tokenizer::tokenize_dc(42);
    int16_t dc42 = Tokenizer::detokenize_dc(t42);
    
    // Test DC=-99
    Token tn99 = Tokenizer::tokenize_dc(-99);
    int16_t dcn99 = Tokenizer::detokenize_dc(tn99);
    
    bool ok = (dc0 == 0 && dc42 == 42 && dcn99 == -99);
    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
    
    if (!ok) {
        std::cout << "  dc0=" << dc0 << " dc42=" << dc42 << " dcn99=" << dcn99 << std::endl;
    }
}

void test_tokenize_ac() {
    std::cout << "Testing AC tokenization... ";
    
    int16_t ac[63] = {0};
    ac[0] = 10;   // First AC
    ac[5] = -20;  // After 4 zeros
    ac[62] = 5;   // Last AC
    
    auto tokens = Tokenizer::tokenize_ac(ac);
    
    int16_t restored[63];
    Tokenizer::detokenize_ac(tokens, restored);
    
    bool ok = true;
    for (int i = 0; i < 63; i++) {
        if (ac[i] != restored[i]) {
            ok = false;
            std::cout << "\n  [" << i << "] orig=" << ac[i] << " restored=" << restored[i];
        }
    }
    
    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
}

void test_headers() {
    std::cout << "Testing FileHeader... ";
    
    FileHeader header;
    header.width = 1920;
    header.height = 1080;
    header.bit_depth = 8;
    header.num_channels = 1;
    header.quality = 75;
    
    uint8_t buffer[48];
    header.write(buffer);
    
    FileHeader restored = FileHeader::read(buffer);
    
    bool ok = (restored.width == 1920 && 
               restored.height == 1080 &&
               restored.quality == 75 &&
               restored.is_valid());
    
    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
}

void test_chunk_directory() {
    std::cout << "Testing ChunkDirectory... ";
    
    ChunkDirectory dir;
    dir.add("QMAT", 48, 130);
    dir.add("TILE", 200, 10000);
    
    auto buffer = dir.serialize();
    ChunkDirectory restored = ChunkDirectory::deserialize(buffer.data(), buffer.size());
    
    bool ok = (restored.entries.size() == 2 &&
               restored.find("QMAT") != nullptr &&
               restored.find("TILE") != nullptr);
    
    std::cout << (ok ? "PASS" : "FAIL") << std::endl;
}

int main() {
    std::cout << "\n=== Phase 5 Component Tests ===\n" << std::endl;
    
    test_zigzag();
    test_dct();
    test_quant();
    test_tokenize_dc();
    test_tokenize_ac();
    test_headers();
    test_chunk_directory();
    
    std::cout << "\nAll component tests complete." << std::endl;
    return 0;
}
