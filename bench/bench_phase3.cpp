#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"
#include "src/entropy/nyans_p/rans_flat_interleaved.h"

#ifdef __AVX2__
#include "src/simd/x86_avx2/rans_decode_avx2.h"
#endif

using namespace hakonyans;
using namespace std::chrono;

struct BenchResult {
    double mib_per_sec;
    size_t compressed_bytes;
};

// --- Token generation ---
std::vector<int> generate_tokens(size_t count, int alphabet_size, std::mt19937& rng) {
    // JPEG-like skewed distribution
    std::vector<double> weights(alphabet_size);
    for (int i = 0; i < alphabet_size; ++i) {
        weights[i] = 1.0 / (1.0 + i);  // Zipf-like
    }
    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    
    std::vector<int> tokens;
    tokens.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        tokens.push_back(dist(rng));
    }
    return tokens;
}

// --- Benchmark functions ---

// N=1 scalar (baseline)
BenchResult bench_n1_decode(const std::vector<int>& tokens, const CDFTable& cdf, int iters) {
    RANSEncoder enc;
    for (int tok : tokens) enc.encode_symbol(cdf, tok);
    auto encoded = enc.finish();
    
    auto start = high_resolution_clock::now();
    for (int iter = 0; iter < iters; ++iter) {
        RANSDecoder dec{std::span<const uint8_t>(encoded)};
        for (size_t i = 0; i < tokens.size(); ++i) {
            volatile int sym = dec.decode_symbol(cdf);
            (void)sym;
        }
    }
    auto end = high_resolution_clock::now();
    
    double elapsed = duration<double>(end - start).count();
    double total_mib = tokens.size() * 2.0 * iters / (1024.0 * 1024.0);
    return {total_mib / elapsed, encoded.size()};
}

// Flat N=8 scalar (CDF linear search)
BenchResult bench_flat_decode(const std::vector<int>& tokens, const CDFTable& cdf, int iters) {
    FlatInterleavedEncoder<8> enc;
    for (int tok : tokens) enc.encode_symbol(cdf, tok);
    auto encoded = enc.finish();
    
    auto start = high_resolution_clock::now();
    for (int iter = 0; iter < iters; ++iter) {
        FlatInterleavedDecoder<8> dec{std::span<const uint8_t>(encoded)};
        for (size_t i = 0; i < tokens.size(); ++i) {
            volatile int sym = dec.decode_symbol(cdf);
            (void)sym;
        }
    }
    auto end = high_resolution_clock::now();
    
    double elapsed = duration<double>(end - start).count();
    double total_mib = tokens.size() * 2.0 * iters / (1024.0 * 1024.0);
    return {total_mib / elapsed, encoded.size()};
}

// Flat N=8 LUT scalar
BenchResult bench_flat_lut_decode(const std::vector<int>& tokens, const CDFTable& cdf,
                                   const SIMDDecodeTable& tbl, int iters) {
    FlatInterleavedEncoder<8> enc;
    for (int tok : tokens) enc.encode_symbol(cdf, tok);
    auto encoded = enc.finish();
    
    auto start = high_resolution_clock::now();
    for (int iter = 0; iter < iters; ++iter) {
        FlatInterleavedDecoder<8> dec{std::span<const uint8_t>(encoded)};
        for (size_t i = 0; i < tokens.size(); ++i) {
            volatile int sym = dec.decode_symbol_lut(tbl);
            (void)sym;
        }
    }
    auto end = high_resolution_clock::now();
    
    double elapsed = duration<double>(end - start).count();
    double total_mib = tokens.size() * 2.0 * iters / (1024.0 * 1024.0);
    return {total_mib / elapsed, encoded.size()};
}

#ifdef __AVX2__
// AVX2 N=8 (per-symbol API)
BenchResult bench_avx2_decode(const std::vector<int>& tokens, const CDFTable& cdf,
                               const SIMDDecodeTable& tbl, int iters) {
    FlatInterleavedEncoder<8> enc;
    for (int tok : tokens) enc.encode_symbol(cdf, tok);
    auto encoded = enc.finish();
    
    auto start = high_resolution_clock::now();
    for (int iter = 0; iter < iters; ++iter) {
        AVX2InterleavedDecoder dec{std::span<const uint8_t>(encoded)};
        for (size_t i = 0; i < tokens.size(); ++i) {
            volatile int sym = dec.decode_symbol(tbl);
            (void)sym;
        }
    }
    auto end = high_resolution_clock::now();
    
    double elapsed = duration<double>(end - start).count();
    double total_mib = tokens.size() * 2.0 * iters / (1024.0 * 1024.0);
    return {total_mib / elapsed, encoded.size()};
}

// AVX2 N=8 (bulk 8-at-a-time API)
BenchResult bench_avx2_bulk_decode(const std::vector<int>& tokens, const CDFTable& cdf,
                                    const SIMDDecodeTable& tbl, int iters) {
    FlatInterleavedEncoder<8> enc;
    for (int tok : tokens) enc.encode_symbol(cdf, tok);
    auto encoded = enc.finish();
    
    alignas(32) int sym_buf[8];
    auto start = high_resolution_clock::now();
    for (int iter = 0; iter < iters; ++iter) {
        AVX2InterleavedDecoder dec{std::span<const uint8_t>(encoded)};
        for (size_t i = 0; i < tokens.size(); i += 8) {
            dec.decode_8symbols(tbl, sym_buf);
        }
    }
    auto end = high_resolution_clock::now();
    
    double elapsed = duration<double>(end - start).count();
    double total_mib = tokens.size() * 2.0 * iters / (1024.0 * 1024.0);
    return {total_mib / elapsed, encoded.size()};
}
#endif

void print_result(const char* name, BenchResult r, double baseline = 0) {
    printf("  %-28s %8.1f MiB/s", name, r.mib_per_sec);
    if (baseline > 0) {
        printf("  (%.2fx)", r.mib_per_sec / baseline);
    }
    printf("  [%zu bytes]\n", r.compressed_bytes);
}

int main(int argc, char** argv) {
    size_t token_count = 1000000;
    int iterations = 30;
    int alphabet_size = 32;
    
    if (argc > 1) token_count = std::atol(argv[1]);
    if (argc > 2) iterations = std::atoi(argv[2]);
    if (argc > 3) alphabet_size = std::atoi(argv[3]);
    
    // Round to multiple of 8
    token_count = (token_count / 8) * 8;
    
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║       HakoNyans rANS Decode Benchmark (Phase 3)        ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Tokens:    %8zu  (%.2f MiB)                        ║\n",
           token_count, token_count * 2.0 / 1024 / 1024);
    printf("║ Alphabet:  %8d                                    ║\n", alphabet_size);
    printf("║ Iters:     %8d                                    ║\n", iterations);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    
    // Generate data
    std::mt19937 rng(42);
    auto tokens = generate_tokens(token_count, alphabet_size, rng);
    
    // Build tables
    std::vector<uint32_t> freq(alphabet_size);
    for (int i = 0; i < alphabet_size; ++i) freq[i] = std::max(1U, 1000U / (1 + i));
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    auto simd_tbl = CDFBuilder::build_simd_table(cdf);
    
    // Warmup
    printf("Warming up...\n");
    {
        FlatInterleavedEncoder<8> enc;
        for (int tok : tokens) enc.encode_symbol(cdf, tok);
        auto encoded = enc.finish();
        FlatInterleavedDecoder<8> dec{std::span<const uint8_t>(encoded)};
        for (size_t i = 0; i < tokens.size(); ++i) dec.decode_symbol_lut(*simd_tbl);
    }
    
    printf("\n=== DECODE BENCHMARK ===\n");
    
    auto r_n1 = bench_n1_decode(tokens, cdf, iterations);
    print_result("N=1 scalar (baseline)", r_n1);
    double baseline = r_n1.mib_per_sec;
    
    auto r_flat = bench_flat_decode(tokens, cdf, iterations);
    print_result("N=8 flat scalar (CDF search)", r_flat, baseline);
    
    auto r_flat_lut = bench_flat_lut_decode(tokens, cdf, *simd_tbl, iterations);
    print_result("N=8 flat scalar (LUT)", r_flat_lut, baseline);
    
#ifdef __AVX2__
    auto r_avx2 = bench_avx2_decode(tokens, cdf, *simd_tbl, iterations);
    print_result("N=8 AVX2 (per-symbol)", r_avx2, baseline);
    
    auto r_avx2_bulk = bench_avx2_bulk_decode(tokens, cdf, *simd_tbl, iterations);
    print_result("N=8 AVX2 (bulk 8x)", r_avx2_bulk, baseline);
    
    printf("\n=== SUMMARY ===\n");
    printf("  LUT vs baseline:        %.2fx\n", r_flat_lut.mib_per_sec / baseline);
    printf("  AVX2 vs baseline:       %.2fx\n", r_avx2.mib_per_sec / baseline);
    printf("  AVX2 bulk vs baseline:  %.2fx\n", r_avx2_bulk.mib_per_sec / baseline);
    printf("  AVX2 bulk vs LUT:       %.2fx\n", r_avx2_bulk.mib_per_sec / r_flat_lut.mib_per_sec);
    
    double best = std::max(r_avx2.mib_per_sec, r_avx2_bulk.mib_per_sec);
    if (best >= 500.0) {
        printf("\n✓ Phase 3 目標達成！ (>500 MiB/s AVX2 decode)\n");
    } else {
        printf("\n→ AVX2 best: %.1f MiB/s（目標 500 MiB/s）\n", best);
    }
#endif
    
    CDFBuilder::cleanup(cdf);
    return 0;
}
