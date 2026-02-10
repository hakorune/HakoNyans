#include <iostream>
#include <vector>
#include <random>
#include "src/entropy/nyans_p/rans_interleaved.h"
#include "src/entropy/nyans_p/rans_tables.h"
#include "src/entropy/nyans_p/tokenization.h"

using namespace hakonyans;

int test_count = 0;
int pass_count = 0;

void check(const char* name, bool passed) {
    test_count++;
    if (passed) {
        std::cout << "✓ " << name << " PASSED\n";
        pass_count++;
    } else {
        std::cout << "✗ " << name << " FAILED\n";
    }
}

void test_interleaved_roundtrip() {
    std::vector<uint32_t> freq{1, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols{0, 1, 2, 1, 0, 1, 1, 2, 0, 1, 2, 0, 1, 0, 2, 1};
    
    InterleavedRANSEncoder<8> encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    
    std::cout << "  Encoded " << symbols.size() << " symbols -> " << encoded.size() << " bytes\n";
    
    InterleavedRANSDecoder<8> decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    check("Interleaved N=8 roundtrip", symbols == decoded);
    CDFBuilder::cleanup(cdf);
}

void test_interleaved_large() {
    std::mt19937 rng(54321);
    
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::uniform_int_distribution<int> dist(0, 7);
    std::vector<int> symbols;
    for (int i = 0; i < 50000; ++i) symbols.push_back(dist(rng));
    
    InterleavedRANSEncoder<8> encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    
    std::cout << "  Encoded " << symbols.size() << " symbols -> " << encoded.size() << " bytes ("
              << (encoded.size() * 100.0 / (symbols.size() * 2)) << "% of uncompressed)\n";
    
    InterleavedRANSDecoder<8> decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    check("Interleaved 50000 symbols (skewed)", symbols == decoded);
    CDFBuilder::cleanup(cdf);
}

void test_tokenization() {
    // 簡単なDCT係数ブロック（64個）
    int16_t coeffs[64] = {
        120, 30, 0, 0, 0, 0, 0, 0,
        -15, 5, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    };
    
    auto tokens = Tokenizer::tokenize_block(coeffs);
    std::cout << "  Block (64 coeffs) -> " << tokens.size() << " tokens\n";
    
    auto reconstructed = Tokenizer::detokenize_block(tokens, 64);
    
    bool match = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffs[i] != reconstructed[i]) {
            std::cout << "  Mismatch at pos " << i << ": expected " << coeffs[i]
                      << " got " << reconstructed[i] << "\n";
            match = false;
            break;
        }
    }
    
    check("Tokenization roundtrip", match);
}

void test_tokenization_zeros() {
    int16_t coeffs[64] = {0};  // 全ゼロ
    
    auto tokens = Tokenizer::tokenize_block(coeffs);
    check("Tokenization all zeros", tokens.size() == 1 && tokens[0].type == TokenType::EOB);
    
    auto reconstructed = Tokenizer::detokenize_block(tokens, 64);
    bool match = true;
    for (int i = 0; i < 64; ++i) {
        if (reconstructed[i] != 0) {
            match = false;
            break;
        }
    }
    check("Detokenization all zeros", match);
}

int main() {
    std::cout << "=== HakoNyans Phase 2: Interleaved rANS + Tokenization Tests ===\n\n";
    
    test_interleaved_roundtrip();
    test_interleaved_large();
    test_tokenization();
    test_tokenization_zeros();
    
    std::cout << "\n" << pass_count << "/" << test_count << " tests passed.\n";
    return (pass_count == test_count) ? 0 : 1;
}
