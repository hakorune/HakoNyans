#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"
#include "src/entropy/nyans_p/rans_flat_interleaved.h"

#ifdef __AVX2__
#include "src/simd/x86_avx2/rans_decode_avx2.h"
#endif

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

// --- Flat Interleaved Tests ---

void test_flat_roundtrip_simple() {
    std::vector<uint32_t> freq{1, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    // 24 symbols (divisible by 8)
    std::vector<int> symbols{0, 1, 2, 1, 0, 1, 1, 2, 0, 1, 2, 0, 1, 0, 2, 1, 0, 1, 1, 2, 0, 1, 2, 1};
    
    FlatInterleavedEncoder<8> encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    
    std::cout << "  Encoded " << symbols.size() << " symbols -> " << encoded.size() << " bytes\n";
    
    FlatInterleavedDecoder<8> decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    check("Flat interleaved roundtrip (simple)", symbols == decoded);
    CDFBuilder::cleanup(cdf);
}

void test_flat_roundtrip_large() {
    std::mt19937 rng(99999);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::uniform_int_distribution<int> dist(0, 8);
    std::vector<int> symbols;
    // 80000 symbols (divisible by 8)
    for (int i = 0; i < 80000; ++i) symbols.push_back(dist(rng));
    
    FlatInterleavedEncoder<8> encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    
    std::cout << "  Encoded " << symbols.size() << " symbols -> " << encoded.size() << " bytes\n";
    
    FlatInterleavedDecoder<8> decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    bool match = (symbols == decoded);
    if (!match) {
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] != decoded[i]) {
                std::cout << "  Mismatch at pos " << i << ": expected " << symbols[i]
                          << " got " << decoded[i] << "\n";
                break;
            }
        }
    }
    check("Flat interleaved roundtrip (80K)", match);
    CDFBuilder::cleanup(cdf);
}

void test_flat_lut_roundtrip() {
    std::mt19937 rng(77777);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    auto simd_tbl = CDFBuilder::build_simd_table(cdf);
    
    std::uniform_int_distribution<int> dist(0, 8);
    std::vector<int> symbols;
    for (int i = 0; i < 10000; ++i) symbols.push_back(dist(rng));
    
    FlatInterleavedEncoder<8> encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    
    // LUT ベースデコード
    FlatInterleavedDecoder<8> decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol_lut(*simd_tbl));
    }
    
    check("Flat LUT roundtrip", symbols == decoded);
    CDFBuilder::cleanup(cdf);
}

#ifdef __AVX2__
void test_avx2_roundtrip() {
    std::mt19937 rng(55555);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    auto simd_tbl = CDFBuilder::build_simd_table(cdf);
    
    std::uniform_int_distribution<int> dist(0, 8);
    std::vector<int> symbols;
    // Must be divisible by 8 for AVX2 batch decode
    for (int i = 0; i < 80000; ++i) symbols.push_back(dist(rng));
    
    FlatInterleavedEncoder<8> encoder;
    for (int sym : symbols) encoder.encode_symbol(cdf, sym);
    auto encoded = encoder.finish();
    
    std::cout << "  AVX2: Encoded " << symbols.size() << " symbols -> " << encoded.size() << " bytes\n";
    
    AVX2InterleavedDecoder decoder{std::span<const uint8_t>(encoded)};
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(*simd_tbl));
    }
    
    bool match = (symbols == decoded);
    if (!match) {
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] != decoded[i]) {
                std::cout << "  AVX2 mismatch at pos " << i << ": expected " << symbols[i]
                          << " got " << decoded[i] << "\n";
                if (i > 5) break;
            }
        }
    }
    check("AVX2 roundtrip (80K)", match);
    CDFBuilder::cleanup(cdf);
}
#endif

int main() {
    std::cout << "=== HakoNyans Phase 3: Flat Interleaved + AVX2 Tests ===\n\n";
    
    test_flat_roundtrip_simple();
    test_flat_roundtrip_large();
    test_flat_lut_roundtrip();
    
#ifdef __AVX2__
    std::cout << "\n--- AVX2 ---\n";
    test_avx2_roundtrip();
#else
    std::cout << "\n(AVX2 not available)\n";
#endif
    
    std::cout << "\n" << pass_count << "/" << test_count << " tests passed.\n";
    return (pass_count == test_count) ? 0 : 1;
}
