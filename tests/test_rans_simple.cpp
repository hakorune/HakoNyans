#include <iostream>
#include <vector>
#include <span>
#include <random>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"

using namespace hakonyans;

int test_count = 0;
int pass_count = 0;

void check(const char* name, const std::vector<int>& expected, const std::vector<int>& actual) {
    test_count++;
    if (expected == actual) {
        std::cout << "✓ " << name << " PASSED\n";
        pass_count++;
    } else {
        std::cout << "✗ " << name << " FAILED\n";
        int mismatches = 0;
        for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
            if (expected[i] != actual[i]) {
                std::cout << "  Position " << i << ": expected " << expected[i] 
                          << " got " << actual[i] << "\n";
                if (++mismatches > 5) { std::cout << "  ...\n"; break; }
            }
        }
    }
}

std::vector<int> roundtrip(const CDFTable& cdf, const std::vector<int>& symbols) {
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    
    RANSDecoder decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    return decoded;
}

void test_simple() {
    // 3 symbols, probability 1/4, 1/2, 1/4
    std::vector<uint32_t> freq{1, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols{0, 1, 2, 1, 0, 1, 1, 2, 0, 1};
    
    RANSEncoder encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    std::cout << "  Encoded " << symbols.size() << " symbols -> " << encoded.size() << " bytes\n";
    
    auto decoded = roundtrip(cdf, symbols);
    check("Simple roundtrip (3 symbols)", symbols, decoded);
    CDFBuilder::cleanup(cdf);
}

void test_binary() {
    std::vector<uint32_t> freq{1, 3};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols{0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1};
    auto decoded = roundtrip(cdf, symbols);
    check("Binary symbols", symbols, decoded);
    CDFBuilder::cleanup(cdf);
}

void test_single_symbol() {
    std::vector<uint32_t> freq{1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols(50, 0);
    auto decoded = roundtrip(cdf, symbols);
    check("Single symbol x50", symbols, decoded);
    CDFBuilder::cleanup(cdf);
}

void test_uniform_256() {
    CDFTable cdf = CDFBuilder::build_uniform(256);
    
    std::vector<int> symbols;
    for (int i = 0; i < 100; ++i) symbols.push_back(i % 256);
    
    auto decoded = roundtrip(cdf, symbols);
    check("Uniform 256 alphabet", symbols, decoded);
    CDFBuilder::cleanup(cdf);
}

void test_random_large() {
    std::mt19937 rng(12345);
    
    // Skewed distribution
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::uniform_int_distribution<int> dist(0, 8);
    std::vector<int> symbols;
    for (int i = 0; i < 10000; ++i) symbols.push_back(dist(rng));
    
    auto decoded = roundtrip(cdf, symbols);
    check("Random 10000 symbols (skewed)", symbols, decoded);
    CDFBuilder::cleanup(cdf);
}

int main() {
    std::cout << "=== HakoNyans rANS Phase 1 Tests ===\n\n";
    
    test_simple();
    test_binary();
    test_single_symbol();
    test_uniform_256();
    test_random_large();
    
    std::cout << "\n" << pass_count << "/" << test_count << " tests passed.\n";
    return (pass_count == test_count) ? 0 : 1;
}
