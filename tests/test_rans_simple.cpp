#include <iostream>
#include <vector>
#include <span>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"

using namespace hakonyans;

void test_simple_roundtrip() {
    std::vector<uint32_t> freq{1, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols{0, 1, 2, 1, 0, 1, 1, 2, 0, 1};
    
    // Encode
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    std::cout << "Encoded " << symbols.size() << " symbols into " << encoded.size() << " bytes\n";
    
    // Decode
    RANSDecoder decoder{std::span(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    // Verify
    bool ok = (symbols == decoded);
    if (ok) {
        std::cout << "✓ Simple roundtrip PASSED\n";
    } else {
        std::cout << "✗ Simple roundtrip FAILED\n";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] != decoded[i]) {
                std::cout << "  Position " << i << ": expected " << symbols[i] << " got " << decoded[i] << "\n";
            }
        }
    }
    
    CDFBuilder::cleanup(cdf);
}

void test_large_alphabet() {
    int alphabet_size = 256;
    std::vector<uint32_t> freq(alphabet_size, 1);
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols;
    for (int i = 0; i < 100; ++i) {
        symbols.push_back(i % alphabet_size);
    }
    
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    
    RANSDecoder decoder{std::span(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    bool ok = (symbols == decoded);
    if (ok) {
        std::cout << "✓ Large alphabet PASSED\n";
    } else {
        std::cout << "✗ Large alphabet FAILED\n";
    }
    
    CDFBuilder::cleanup(cdf);
}

int main() {
    std::cout << "=== rANS Phase 1 Tests ===\n\n";
    test_simple_roundtrip();
    test_large_alphabet();
    std::cout << "\nDone.\n";
    return 0;
}
