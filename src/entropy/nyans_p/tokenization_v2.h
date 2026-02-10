#pragma once

#include <cstdint>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace hakonyans {

enum class TokenType : uint8_t {
    ZRUN_0 = 0, ZRUN_62 = 62, ZRUN_63 = 63,
    MAGC_0 = 64, MAGC_11 = 75,
};

struct Token {
    TokenType type;
    uint16_t raw_bits;
    uint8_t raw_bits_count;
    Token(TokenType t = TokenType::ZRUN_63, uint16_t bits = 0, uint8_t bits_count = 0)
        : type(t), raw_bits(bits), raw_bits_count(bits_count) {}
};

struct TokenWithBand {
    Token token;
    int band;
};

class Tokenizer {
public:
    static void tokenize_ac_with_bands(const int16_t ac_coeffs[63], std::vector<TokenWithBand>& tokens) {
        int pos = 0;
        while (pos < 63) {
            int zrun = 0;
            while (pos + zrun < 63 && ac_coeffs[pos + zrun] == 0) zrun++;
            
            if (pos + zrun == 63) {
                int band = get_band(pos);
                tokens.push_back({Token(TokenType::ZRUN_63, 0, 0), band});
                break;
            }
            
            // If ZRUN crosses band boundary, split it
            while (zrun > 0) {
                int band = get_band(pos);
                int band_end = (band == 0) ? 15 : (band == 1) ? 31 : 63;
                int can_take = std::min(zrun, band_end - pos);
                
                if (can_take > 0) {
                    tokens.push_back({Token(static_cast<TokenType>(can_take), 0, 0), band});
                    pos += can_take;
                    zrun -= can_take;
                } else {
                    // Must be exactly at boundary, take a ZRUN_0 to "move" to next band if needed
                    // but usually ZRUN > 0 here. Just move pos.
                    break; 
                }
                // If we reached boundary but still have zrun, loop will continue with next band
            }
            
            // Now at pos, ac_coeffs[pos] is non-zero
            int band = get_band(pos);
            int16_t v = ac_coeffs[pos];
            uint16_t abs_v = std::abs(v);
            int magc = get_magc(abs_v);
            uint16_t sign_bit = (v > 0) ? 0 : 1;
            uint16_t rem = (magc > 0) ? (abs_v - (1 << (magc - 1))) : 0;
            uint16_t raw_bits = (sign_bit << magc) | rem;
            tokens.push_back({Token(static_cast<TokenType>(64 + magc), raw_bits, (uint8_t)(1 + magc)), band});
            pos++;
        }
    }

    static int get_band(int pos) { return (pos < 15) ? 0 : (pos < 31) ? 1 : 2; }

    static Token tokenize_dc(int16_t dc) {
        if (dc == 0) return Token(TokenType::MAGC_0, 0, 0);
        uint16_t abs_v = std::abs(dc); int magc = get_magc(abs_v);
        uint16_t sign_bit = (dc > 0) ? 0 : 1;
        uint16_t rem = (magc > 0) ? (abs_v - (1 << (magc - 1))) : 0;
        return Token(static_cast<TokenType>(64 + magc), (sign_bit << magc) | rem, (uint8_t)(1 + magc));
    }

    static std::vector<Token> tokenize_ac(const int16_t ac_coeffs[63]) {
        std::vector<Token> tokens; int pos = 0;
        while (pos < 63) {
            int zrun = 0; while (pos + zrun < 63 && ac_coeffs[pos + zrun] == 0) zrun++;
            if (pos + zrun == 63) { tokens.emplace_back(TokenType::ZRUN_63, 0, 0); break; }
            tokens.emplace_back(static_cast<TokenType>(zrun), 0, 0); pos += zrun;
            int16_t v = ac_coeffs[pos]; uint16_t abs_v = std::abs(v); int magc = get_magc(abs_v);
            uint16_t sign_bit = (v > 0) ? 0 : 1; uint16_t rem = (magc > 0) ? (abs_v - (1 << (magc - 1))) : 0;
            tokens.emplace_back(static_cast<TokenType>(64 + magc), (sign_bit << magc) | rem, (uint8_t)(1 + magc));
            pos++;
        }
        if (tokens.empty() || tokens.back().type != TokenType::ZRUN_63) tokens.emplace_back(TokenType::ZRUN_63, 0, 0);
        return tokens;
    }

    static int16_t detokenize_dc(const Token& token) {
        if (token.type == TokenType::MAGC_0) return 0;
        int magc = (int)token.type - 64;
        uint16_t sign_bit = (token.raw_bits >> magc) & 1;
        uint16_t rem = token.raw_bits & ((1 << magc) - 1);
        uint16_t abs_v = (magc > 0) ? ((1 << (magc - 1)) + rem) : 0;
        return (sign_bit == 0) ? abs_v : -abs_v;
    }

    static void detokenize_ac(const std::vector<Token>& tokens, int16_t ac_coeffs[63]) {
        std::fill(ac_coeffs, ac_coeffs + 63, 0); int pos = 0;
        for (size_t i = 0; i < tokens.size() && pos < 63; ++i) {
            int type = (int)tokens[i].type;
            if (type == 63) break;
            if (type < 63) {
                pos += type;
                if (++i >= tokens.size()) break;
                int magc = (int)tokens[i].type - 64;
                uint16_t sign = (tokens[i].raw_bits >> magc) & 1;
                uint16_t rem = tokens[i].raw_bits & ((1 << magc) - 1);
                uint16_t abs_v = (magc > 0) ? ((1 << (magc - 1)) + rem) : 0;
                if (pos < 63) ac_coeffs[pos++] = (sign == 0) ? abs_v : -abs_v;
            }
        }
    }

private:
    static int get_magc(uint16_t abs_v) {
        if (abs_v == 0) return 0;
        int magc = 32 - __builtin_clz(abs_v);
        return (magc > 11) ? 11 : magc;
    }
};

} // namespace hakonyans