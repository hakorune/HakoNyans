#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_interleaved.h"
#include "src/entropy/nyans_p/rans_tables.h"

using namespace hakonyans;
using namespace std::chrono;

// ランダムトークン列生成（スキュー分布）
std::vector<int> generate_tokens(size_t count, std::mt19937& rng) {
    // JPEG係数ライクな分布（低周波 = 高頻度）
    std::discrete_distribution<int> dist{
        1000, 800, 600, 400, 300, 200, 150, 100,  // 0-7
        80, 60, 50, 40, 30, 25, 20, 15,           // 8-15
        12, 10, 8, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1  // 16-31
    };
    
    std::vector<int> tokens;
    tokens.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        tokens.push_back(dist(rng));
    }
    return tokens;
}

// エンコード速度測定
double bench_encode_n1(const std::vector<int>& tokens, const CDFTable& cdf, int iterations) {
    auto start = high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        RANSEncoder encoder;
        for (int tok : tokens) {
            encoder.encode_symbol(cdf, tok);
        }
        auto encoded = encoder.finish();
        // prevent optimization
        if (encoded.empty()) std::abort();
    }
    
    auto end = high_resolution_clock::now();
    double elapsed = duration<double>(end - start).count();
    
    // MiB/s = (tokens * sizeof(int16_t) * iterations) / (1024^2) / elapsed
    double total_bytes = tokens.size() * sizeof(int16_t) * iterations;
    return total_bytes / (1024.0 * 1024.0) / elapsed;
}

double bench_encode_n8(const std::vector<int>& tokens, const CDFTable& cdf, int iterations) {
    auto start = high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        InterleavedRANSEncoder<8> encoder;
        for (int tok : tokens) {
            encoder.encode_symbol(cdf, tok);
        }
        auto encoded = encoder.finish();
        if (encoded.empty()) std::abort();
    }
    
    auto end = high_resolution_clock::now();
    double elapsed = duration<double>(end - start).count();
    double total_bytes = tokens.size() * sizeof(int16_t) * iterations;
    return total_bytes / (1024.0 * 1024.0) / elapsed;
}

// デコード速度測定（コンストラクタコスト除外版）
double bench_decode_n1_streaming(const std::vector<uint8_t>& encoded, size_t token_count,
                                   const CDFTable& cdf, int iterations) {
    auto start = high_resolution_clock::now();
    
    // 1回だけ初期化、iterations回連続デコード
    RANSEncoder enc_tmp;
    for (size_t i = 0; i < token_count * iterations; ++i) {
        enc_tmp.encode_symbol(cdf, i % 32);  // ダミーデータ
    }
    auto big_encoded = enc_tmp.finish();
    
    RANSDecoder decoder{std::span<const uint8_t>(big_encoded)};
    auto decode_start = high_resolution_clock::now();
    for (size_t i = 0; i < token_count * iterations; ++i) {
        int tok = decoder.decode_symbol(cdf);
        if (tok < 0 || tok >= 32) std::abort();
    }
    auto decode_end = high_resolution_clock::now();
    
    double elapsed = duration<double>(decode_end - decode_start).count();
    double total_bytes = token_count * sizeof(int16_t) * iterations;
    return total_bytes / (1024.0 * 1024.0) / elapsed;
}

double bench_decode_n8_streaming(const std::vector<uint8_t>& encoded, size_t token_count,
                                   const CDFTable& cdf, int iterations) {
    // 1回だけ初期化、iterations回連続デコード
    InterleavedRANSEncoder<8> enc_tmp;
    for (size_t i = 0; i < token_count * iterations; ++i) {
        enc_tmp.encode_symbol(cdf, i % 32);
    }
    auto big_encoded = enc_tmp.finish();
    
    InterleavedRANSDecoder<8> decoder{std::span<const uint8_t>(big_encoded)};
    auto decode_start = high_resolution_clock::now();
    for (size_t i = 0; i < token_count * iterations; ++i) {
        int tok = decoder.decode_symbol(cdf);
        if (tok < 0 || tok >= 32) std::abort();
    }
    auto decode_end = high_resolution_clock::now();
    
    double elapsed = duration<double>(decode_end - decode_start).count();
    double total_bytes = token_count * sizeof(int16_t) * iterations;
    return total_bytes / (1024.0 * 1024.0) / elapsed;
}

// デコード速度測定
double bench_decode_n1(const std::vector<uint8_t>& encoded, size_t token_count,
                       const CDFTable& cdf, int iterations) {
    std::vector<int> decoded;
    decoded.reserve(token_count);
    
    auto start = high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        decoded.clear();
        RANSDecoder decoder{std::span<const uint8_t>(encoded.data(), encoded.size())};
        for (size_t i = 0; i < token_count; ++i) {
            decoded.push_back(decoder.decode_symbol(cdf));
        }
    }
    
    auto end = high_resolution_clock::now();
    double elapsed = duration<double>(end - start).count();
    double total_bytes = token_count * sizeof(int16_t) * iterations;
    
    // Verify decode worked
    if (decoded.size() != token_count) {
        std::cerr << "ERROR: decoded size mismatch\n";
        std::abort();
    }
    
    return total_bytes / (1024.0 * 1024.0) / elapsed;
}

double bench_decode_n8(const std::vector<uint8_t>& encoded, size_t token_count,
                       const CDFTable& cdf, int iterations) {
    std::vector<int> decoded;
    decoded.reserve(token_count);
    
    auto start = high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        decoded.clear();
        InterleavedRANSDecoder<8> decoder{std::span<const uint8_t>(encoded.data(), encoded.size())};
        for (size_t i = 0; i < token_count; ++i) {
            decoded.push_back(decoder.decode_symbol(cdf));
        }
    }
    
    auto end = high_resolution_clock::now();
    double elapsed = duration<double>(end - start).count();
    double total_bytes = token_count * sizeof(int16_t) * iterations;
    
    if (decoded.size() != token_count) {
        std::cerr << "ERROR: decoded size mismatch\n";
        std::abort();
    }
    
    return total_bytes / (1024.0 * 1024.0) / elapsed;
}

int main(int argc, char** argv) {
    // パラメータ
    size_t token_count = 1000000;  // 1M tokens ≈ 2 MiB
    int iterations = 50;
    bool force_scalar = (std::getenv("HAKONYANS_FORCE_SCALAR") != nullptr);
    
    if (argc > 1) token_count = std::atol(argv[1]);
    if (argc > 2) iterations = std::atoi(argv[2]);
    
    std::cout << "=== HakoNyans Entropy Benchmark ===\n";
    std::cout << "Tokens: " << token_count << " (" << (token_count * 2.0 / 1024 / 1024) << " MiB)\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Force scalar: " << (force_scalar ? "YES" : "NO") << "\n\n";
    
    // トークン生成
    std::mt19937 rng(42);
    auto tokens = generate_tokens(token_count, rng);
    
    // CDF構築（32シンボル、スキュー分布）
    std::vector<uint32_t> freq{
        1000, 800, 600, 400, 300, 200, 150, 100,
        80, 60, 50, 40, 30, 25, 20, 15,
        12, 10, 8, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1
    };
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    // エンコード準備（N=1/N=8）
    RANSEncoder enc1;
    for (int tok : tokens) enc1.encode_symbol(cdf, tok);
    auto encoded1 = enc1.finish();
    
    InterleavedRANSEncoder<8> enc8;
    for (int tok : tokens) enc8.encode_symbol(cdf, tok);
    auto encoded8 = enc8.finish();
    
    std::cout << "Compressed size:\n";
    std::cout << "  N=1: " << encoded1.size() << " bytes ("
              << (encoded1.size() * 100.0 / (token_count * 2)) << "%)\n";
    std::cout << "  N=8: " << encoded8.size() << " bytes ("
              << (encoded8.size() * 100.0 / (token_count * 2)) << "%)\n\n";
    
    // ベンチマーク実行
    std::cout << "=== ENCODE ===\n";
    double encode_n1 = bench_encode_n1(tokens, cdf, iterations);
    std::cout << "N=1 (scalar):  " << encode_n1 << " MiB/s\n";
    
    double encode_n8 = bench_encode_n8(tokens, cdf, iterations);
    std::cout << "N=8 (interleaved):  " << encode_n8 << " MiB/s  (speedup: "
              << (encode_n8 / encode_n1) << "x)\n\n";
    
    std::cout << "=== DECODE ===\n";
    double decode_n1 = bench_decode_n1(encoded1, token_count, cdf, iterations);
    std::cout << "N=1 (scalar):  " << decode_n1 << " MiB/s\n";
    
    double decode_n8 = bench_decode_n8(encoded8, token_count, cdf, iterations);
    std::cout << "N=8 (interleaved):  " << decode_n8 << " MiB/s  (speedup: "
              << (decode_n8 / decode_n1) << "x)\n\n";
    
    std::cout << "=== DECODE (Streaming, no init overhead) ===\n";
    double decode_n1_stream = bench_decode_n1_streaming(encoded1, token_count, cdf, iterations);
    std::cout << "N=1 (scalar):  " << decode_n1_stream << " MiB/s\n";
    
    double decode_n8_stream = bench_decode_n8_streaming(encoded8, token_count, cdf, iterations);
    std::cout << "N=8 (interleaved):  " << decode_n8_stream << " MiB/s  (speedup: "
              << (decode_n8_stream / decode_n1_stream) << "x)\n\n";
    
    // 目標チェック
    std::cout << "=== RESULTS ===\n";
    if (decode_n8_stream >= 500.0) {
        std::cout << "✓ Phase 2 目標達成！ (>500 MiB/s decode streaming)\n";
    } else {
        std::cout << "✗ Phase 2 目標未達（目標: 500 MiB/s, 実測: " << decode_n8_stream << " MiB/s）\n";
    }
    
    CDFBuilder::cleanup(cdf);
    return 0;
}
