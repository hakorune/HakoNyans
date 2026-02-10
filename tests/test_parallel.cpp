#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"
#include "src/entropy/nyans_p/rans_flat_interleaved.h"
#include "src/entropy/nyans_p/pindex.h"
#include "src/entropy/nyans_p/parallel_decode.h"

using namespace hakonyans;
using namespace std::chrono;

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

// Helper: encode tokens
std::vector<uint8_t> encode_tokens(const std::vector<int>& tokens, const CDFTable& cdf) {
    FlatInterleavedEncoder<8> enc;
    for (int tok : tokens) enc.encode_symbol(cdf, tok);
    return enc.finish();
}

void test_pindex_build() {
    std::mt19937 rng(11111);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::uniform_int_distribution<int> dist(0, 7);
    std::vector<int> symbols;
    for (int i = 0; i < 10000; ++i) symbols.push_back(dist(rng));
    // Round to multiple of 8
    symbols.resize((symbols.size() / 8) * 8);
    
    auto encoded = encode_tokens(symbols, cdf);
    
    // Build P-Index with interval=1024 tokens
    auto pindex = PIndexBuilder::build(
        std::span<const uint8_t>(encoded), cdf, symbols.size(), 1024);
    
    std::cout << "  Checkpoints: " << pindex.checkpoints.size()
              << " (interval=1024, tokens=" << symbols.size() << ")\n";
    
    // Verify checkpoint count
    int expected_cp = (symbols.size() / 1024);  // +1 for start, -1 because last boundary excluded
    check("P-Index checkpoint count", 
          pindex.checkpoints.size() >= 2 && pindex.checkpoints.size() <= (size_t)expected_cp + 2);
    
    // First checkpoint should be at position 0
    check("P-Index first checkpoint at 0",
          pindex.checkpoints[0].byte_offset == 0 && pindex.checkpoints[0].token_index == 0);
    
    CDFBuilder::cleanup(cdf);
}

void test_pindex_serialize() {
    PIndex pindex;
    pindex.total_tokens = 10000;
    pindex.total_bytes = 5000;
    pindex.checkpoints.push_back(Checkpoint{0, 0, {1, 2, 3, 4, 5, 6, 7, 8}});
    pindex.checkpoints.push_back(Checkpoint{1000, 2000, {9, 10, 11, 12, 13, 14, 15, 16}});
    
    auto serialized = PIndexCodec::serialize(pindex);
    auto deserialized = PIndexCodec::deserialize(std::span<const uint8_t>(serialized));
    
    bool match = (deserialized.total_tokens == 10000 &&
                  deserialized.total_bytes == 5000 &&
                  deserialized.checkpoints.size() == 2 &&
                  deserialized.checkpoints[0].byte_offset == 0 &&
                  deserialized.checkpoints[1].byte_offset == 1000 &&
                  deserialized.checkpoints[1].states[7] == 16);
    
    check("P-Index serialize/deserialize roundtrip", match);
}

void test_parallel_decode_single_thread() {
    std::mt19937 rng(22222);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::uniform_int_distribution<int> dist(0, 7);
    std::vector<int> symbols;
    for (int i = 0; i < 8000; ++i) symbols.push_back(dist(rng));
    
    auto encoded = encode_tokens(symbols, cdf);
    auto pindex = PIndexBuilder::build(
        std::span<const uint8_t>(encoded), cdf, symbols.size(), 1024);
    
    // Single-thread decode via P-Index
    auto decoded = ParallelDecoder::decode(
        std::span<const uint8_t>(encoded), pindex, cdf, 1);
    
    check("Parallel decode (1 thread)", symbols == decoded);
    CDFBuilder::cleanup(cdf);
}

void test_parallel_decode_multi_thread() {
    std::mt19937 rng(33333);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    auto simd_tbl = CDFBuilder::build_simd_table(cdf);
    
    std::uniform_int_distribution<int> dist(0, 7);
    std::vector<int> symbols;
    for (int i = 0; i < 80000; ++i) symbols.push_back(dist(rng));
    
    auto encoded = encode_tokens(symbols, cdf);
    auto pindex = PIndexBuilder::build(
        std::span<const uint8_t>(encoded), cdf, symbols.size(), 4096);
    
    std::cout << "  Checkpoints: " << pindex.checkpoints.size() << "\n";
    
    // Multi-thread decode
    for (int threads : {1, 2, 4, 8}) {
        auto decoded = ParallelDecoder::decode(
            std::span<const uint8_t>(encoded), pindex, cdf, threads);
        
        bool match = (symbols == decoded);
        if (!match) {
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (symbols[i] != decoded[i]) {
                    std::cout << "  Mismatch at pos " << i << " (thread=" << threads
                              << "): expected " << symbols[i] << " got " << decoded[i] << "\n";
                    break;
                }
            }
        }
        char name[64];
        snprintf(name, sizeof(name), "Parallel decode (%d threads, 80K)", threads);
        check(name, match);
    }
    
    // LUT version
    auto decoded_lut = ParallelDecoder::decode_lut(
        std::span<const uint8_t>(encoded), pindex, *simd_tbl, 4);
    check("Parallel decode LUT (4 threads)", symbols == decoded_lut);
    
    CDFBuilder::cleanup(cdf);
}

void bench_parallel_scaling() {
    std::mt19937 rng(44444);
    std::vector<uint32_t> freq{100, 50, 25, 12, 6, 3, 2, 1, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    auto simd_tbl = CDFBuilder::build_simd_table(cdf);
    
    std::uniform_int_distribution<int> dist(0, 8);
    size_t token_count = 4000000;  // 4M tokens
    std::vector<int> symbols;
    symbols.reserve(token_count);
    for (size_t i = 0; i < token_count; ++i) symbols.push_back(dist(rng));
    
    auto encoded = encode_tokens(symbols, cdf);
    auto pindex = PIndexBuilder::build(
        std::span<const uint8_t>(encoded), cdf, symbols.size(), 8192);
    
    std::cout << "\n=== PARALLEL SCALING BENCHMARK ===\n";
    std::cout << "  Tokens: " << token_count << " (" << (token_count * 2.0 / 1024 / 1024) << " MiB)\n";
    std::cout << "  Checkpoints: " << pindex.checkpoints.size() << "\n\n";
    
    double baseline = 0;
    for (int threads : {1, 2, 4, 8, 16}) {
        auto start = high_resolution_clock::now();
        int iters = 10;
        for (int it = 0; it < iters; ++it) {
            auto decoded = ParallelDecoder::decode_lut(
                std::span<const uint8_t>(encoded), pindex, *simd_tbl, threads);
        }
        auto end = high_resolution_clock::now();
        
        double elapsed = duration<double>(end - start).count();
        double mib_per_sec = token_count * 2.0 * iters / (1024.0 * 1024.0) / elapsed;
        
        if (threads == 1) baseline = mib_per_sec;
        printf("  %2d threads: %8.1f MiB/s  (%.2fx)\n",
               threads, mib_per_sec, mib_per_sec / baseline);
    }
    
    CDFBuilder::cleanup(cdf);
}

int main() {
    std::cout << "=== HakoNyans Phase 4: P-Index Parallel Decode Tests ===\n\n";
    
    test_pindex_build();
    test_pindex_serialize();
    test_parallel_decode_single_thread();
    test_parallel_decode_multi_thread();
    
    std::cout << "\n" << pass_count << "/" << test_count << " tests passed.\n";
    
    if (pass_count == test_count) {
        bench_parallel_scaling();
    }
    
    return (pass_count == test_count) ? 0 : 1;
}
