#include <gtest/gtest.h>
#include <random>
#include <vector>
#include "src/entropy/nyans_p/rans_core.h"
#include "src/entropy/nyans_p/rans_tables.h"

using namespace hakonyans;

/**
 * rANS 往復テスト
 * 
 * ランダムシンボル列をエンコードしてデコード、
 * 元のシンボル列に一致することを確認
 */
class RANSRoundtripTest : public ::testing::Test {
protected:
    std::mt19937 rng{42};  // 固定シード
};

TEST_F(RANSRoundtripTest, SimpleRoundtrip) {
    // 3 シンボル、確率 1/4, 1/2, 1/4
    std::vector<uint32_t> freq{1, 2, 1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    // テスト用シンボル列
    std::vector<int> symbols{0, 1, 2, 1, 0, 1, 1, 2, 0, 1};
    
    // エンコード
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    
    // デコード
    RANSDecoder decoder(encoded);
    std::vector<int> decoded;
    
    // デコーダはシンボル列の長さを知らないので、
    // EOF まで読む（簡易版）
    // 実際のプロトコルではシンボル数をヘッダに含める
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    // 検証
    EXPECT_EQ(symbols, decoded);
    
    CDFBuilder::cleanup(cdf);
}

TEST_F(RANSRoundtripTest, LargeSymbolSet) {
    // 256 シンボル、ほぼ均一分布
    int alphabet_size = 256;
    std::vector<uint32_t> freq(alphabet_size, 1);
    freq[0] = 2;  // 少し偏らせる
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    // ランダムシンボル列生成
    std::uniform_int_distribution<int> dist(0, alphabet_size - 1);
    std::vector<int> symbols;
    for (int i = 0; i < 1000; ++i) {
        symbols.push_back(dist(rng));
    }
    
    // エンコード
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    
    // デコード
    RANSDecoder decoder(encoded);
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    // 検証
    EXPECT_EQ(symbols, decoded);
    
    CDFBuilder::cleanup(cdf);
}

TEST_F(RANSRoundtripTest, SingleSymbol) {
    // 1 シンボルのみ
    std::vector<uint32_t> freq{1};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    std::vector<int> symbols(100, 0);
    
    // エンコード
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    
    // デコード
    RANSDecoder decoder(encoded);
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    // 検証
    EXPECT_EQ(symbols, decoded);
    
    CDFBuilder::cleanup(cdf);
}

TEST_F(RANSRoundtripTest, BinarySymbols) {
    // 2 シンボル、確率 1/3, 2/3
    std::vector<uint32_t> freq{1, 2};
    CDFTable cdf = CDFBuilder::build_from_freq(freq);
    
    // ランダムバイナリシンボル列
    std::uniform_int_distribution<int> dist(0, 1);
    std::vector<int> symbols;
    for (int i = 0; i < 500; ++i) {
        symbols.push_back(dist(rng));
    }
    
    // エンコード
    RANSEncoder encoder;
    for (int sym : symbols) {
        encoder.encode_symbol(cdf, sym);
    }
    std::vector<uint8_t> encoded = encoder.finish();
    
    // デコード
    RANSDecoder decoder(encoded);
    std::vector<int> decoded;
    for (size_t i = 0; i < symbols.size(); ++i) {
        decoded.push_back(decoder.decode_symbol(cdf));
    }
    
    // 検証
    EXPECT_EQ(symbols, decoded);
    
    CDFBuilder::cleanup(cdf);
}
