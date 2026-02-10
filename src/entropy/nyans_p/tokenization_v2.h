#pragma once

#include <cstdint>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace hakonyans {

/**
 * トークン種別（ZRUN 統合版、CDF 分離対応）
 * 
 * ZRUN と MAGC のみ rANS で符号化。
 * SIGN と REM は raw bits（rANS 外）。
 */
enum class TokenType : uint8_t {
    // ZRUN: 0個〜63個のゼロラン（EOB 含む）
    ZRUN_0 = 0,     // 直後に非ゼロ（ゼロラン長 0）
    ZRUN_1 = 1,
    // ...
    ZRUN_62 = 62,   // ゼロが 62個
    ZRUN_63 = 63,   // EOB（残り全部ゼロ）
    
    // MAGC: 係数絶対値のクラス（0..11）
    MAGC_0 = 64,    // |v| = 0
    MAGC_1 = 65,    // |v| ∈ [1, 2)
    MAGC_2 = 66,    // |v| ∈ [2, 4)
    // ...
    MAGC_11 = 75,   // |v| ∈ [1024, 2048)
};

/**
 * トークン（記号 + 追加の raw bits）
 */
struct Token {
    TokenType type;
    uint16_t raw_bits;      // SIGN (1bit) + REM (0..11bits)
    uint8_t raw_bits_count; // Total raw bits count
    
    Token(TokenType t = TokenType::ZRUN_63, uint16_t bits = 0, uint8_t bits_count = 0)
        : type(t), raw_bits(bits), raw_bits_count(bits_count) {}
};

/**
 * DC/AC 分離トークナイザ（ZRUN 統合版）
 * 
 * DC: MAGC + SIGN + REM のみ（ZRUN なし）
 * AC: ZRUN + MAGC + SIGN + REM
 */
class Tokenizer {
public:
    /**
     * DC 係数をトークン化（単一係数、ZRUN なし）
     * @param dc DC coefficient
     * @return Token (MAGC + SIGN + REM in raw_bits)
     */
    static Token tokenize_dc(int16_t dc) {
        if (dc == 0) {
            return Token(TokenType::MAGC_0, 0, 0);
        }
        
        uint16_t abs_v = std::abs(dc);
        int magc = get_magc(abs_v);
        
        // SIGN (1bit) + REM (magc bits)
        uint16_t sign_bit = (dc > 0) ? 0 : 1;
        uint16_t rem = (magc > 0) ? (abs_v - (1 << (magc - 1))) : 0;
        uint16_t raw_bits = (sign_bit << magc) | rem;
        uint8_t raw_bits_count = 1 + magc;  // 1 (SIGN) + magc (REM)
        
        TokenType token_type = static_cast<TokenType>(
            static_cast<int>(TokenType::MAGC_0) + magc);
        
        return Token(token_type, raw_bits, raw_bits_count);
    }

    /**
     * AC 係数ブロック（63個）をトークン化
     * @param ac_coeffs AC coefficients in zigzag order [1..63]
     * @return トークン列（ZRUN + MAGC）
     */
    static std::vector<Token> tokenize_ac(const int16_t ac_coeffs[63]) {
        std::vector<Token> tokens;
        tokens.reserve(63);
        
        int pos = 0;
        while (pos < 63) {
            // ゼロラン検出
            int zrun = 0;
            while (pos + zrun < 63 && ac_coeffs[pos + zrun] == 0) {
                zrun++;
            }
            
            // EOB 判定（残り全部 0）
            if (pos + zrun == 63) {
                tokens.emplace_back(TokenType::ZRUN_63, 0, 0);
                break;
            }
            
            // ZRUN トークン
            TokenType zrun_type = static_cast<TokenType>(
                static_cast<int>(TokenType::ZRUN_0) + zrun);
            tokens.emplace_back(zrun_type, 0, 0);
            pos += zrun;
            
            // 非ゼロ係数
            int16_t v = ac_coeffs[pos];
            uint16_t abs_v = std::abs(v);
            int magc = get_magc(abs_v);
            
            // SIGN (1bit) + REM (magc bits)
            uint16_t sign_bit = (v > 0) ? 0 : 1;
            uint16_t rem = (magc > 0) ? (abs_v - (1 << (magc - 1))) : 0;
            uint16_t raw_bits = (sign_bit << magc) | rem;
            uint8_t raw_bits_count = 1 + magc;
            
            TokenType magc_type = static_cast<TokenType>(
                static_cast<int>(TokenType::MAGC_0) + magc);
            tokens.emplace_back(magc_type, raw_bits, raw_bits_count);
            
            pos++;
        }
        
        return tokens;
    }

    /**
     * DC トークンから係数を復元
     */
    static int16_t detokenize_dc(const Token& token) {
        if (token.type == TokenType::MAGC_0) {
            return 0;
        }
        
        int magc = static_cast<int>(token.type) - static_cast<int>(TokenType::MAGC_0);
        
        // SIGN (最上位ビット) + REM (下位ビット)
        uint16_t sign_bit = (token.raw_bits >> magc) & 1;
        uint16_t rem = token.raw_bits & ((1 << magc) - 1);
        uint16_t abs_v = (magc > 0) ? ((1 << (magc - 1)) + rem) : 0;
        
        return (sign_bit == 0) ? abs_v : -abs_v;
    }

    /**
     * AC トークン列から係数を復元
     */
    static void detokenize_ac(const std::vector<Token>& tokens, int16_t ac_coeffs[63]) {
        for (int i = 0; i < 63; i++) {
            ac_coeffs[i] = 0;
        }
        
        int pos = 0;
        size_t token_idx = 0;
        
        while (token_idx < tokens.size() && pos < 63) {
            const Token& tok = tokens[token_idx++];
            
            // ZRUN
            if (static_cast<int>(tok.type) <= 63) {
                int zrun = static_cast<int>(tok.type);
                if (zrun == 63) {
                    // EOB
                    break;
                }
                pos += zrun;
                
                // 次は MAGC のはず
                if (token_idx >= tokens.size() || pos >= 63) break;
                
                const Token& magc_tok = tokens[token_idx++];
                int magc = static_cast<int>(magc_tok.type) - static_cast<int>(TokenType::MAGC_0);
                
                // SIGN + REM
                uint16_t sign_bit = (magc_tok.raw_bits >> magc) & 1;
                uint16_t rem = magc_tok.raw_bits & ((1 << magc) - 1);
                uint16_t abs_v = (magc > 0) ? ((1 << (magc - 1)) + rem) : 0;
                
                ac_coeffs[pos++] = (sign_bit == 0) ? abs_v : -abs_v;
            }
        }
    }

    /**
     * 完全なブロック（DC + AC）をトークン化（レガシー互換用）
     */
    static std::vector<Token> tokenize_block(const int16_t* coeffs, int size = 64) {
        std::vector<Token> tokens;
        
        // DC
        tokens.push_back(tokenize_dc(coeffs[0]));
        
        // AC
        auto ac_tokens = tokenize_ac(&coeffs[1]);
        tokens.insert(tokens.end(), ac_tokens.begin(), ac_tokens.end());
        
        return tokens;
    }

    /**
     * トークン列から完全なブロックを復元（レガシー互換用）
     */
    static std::vector<int16_t> detokenize_block(const std::vector<Token>& tokens, int size = 64) {
        std::vector<int16_t> coeffs(size, 0);
        
        if (tokens.empty()) return coeffs;
        
        // DC
        coeffs[0] = detokenize_dc(tokens[0]);
        
        // AC
        if (tokens.size() > 1) {
            std::vector<Token> ac_tokens(tokens.begin() + 1, tokens.end());
            detokenize_ac(ac_tokens, &coeffs[1]);
        }
        
        return coeffs;
    }

private:
    /**
     * 係数絶対値から MAGC クラスを計算
     * MAGC = floor(log2(abs_v)) + 1, clamped to [0, 11]
     */
    static int get_magc(uint16_t abs_v) {
        if (abs_v == 0) return 0;
        // __builtin_clz counts leading zeros in uint32_t
        int magc = 32 - __builtin_clz(abs_v);
        return (magc > 11) ? 11 : magc;
    }
};

} // namespace hakonyans
