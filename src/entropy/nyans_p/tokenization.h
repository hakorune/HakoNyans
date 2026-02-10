#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

namespace hakonyans {

/**
 * トークン種別（小アルファベット設計）
 * 
 * rANS で扱うのはこれらの記号だけ。
 * 係数の下位ビット（REM）は raw bits として別途処理。
 */
enum class TokenType : uint8_t {
    EOB = 0,        // End of Block（残り全部0）
    RUN_0 = 1,      // 0が0個続く（= 非ゼロ係数が直後）
    RUN_1 = 2,      // 0が1個
    // ...
    RUN_15 = 16,    // 0が15個
    RUN_ESC = 17,   // 16個以上（追加の raw bits で長さ指定）
    
    MAGC_0 = 18,    // |v| = 0
    MAGC_1 = 19,    // |v| ∈ [1, 2)
    MAGC_2 = 20,    // |v| ∈ [2, 4)
    // ...
    MAGC_11 = 29,   // |v| ∈ [1024, 2048)
    
    SIGN_POS = 30,  // 符号 +
    SIGN_NEG = 31,  // 符号 -
};

/**
 * トークン（記号 + 追加の raw bits）
 */
struct Token {
    TokenType type;
    uint16_t raw_bits;  // REM や RUN_ESC の追加ビット
    uint8_t raw_bits_count;
    
    Token(TokenType t, uint16_t bits = 0, uint8_t bits_count = 0)
        : type(t), raw_bits(bits), raw_bits_count(bits_count) {}
};

/**
 * 量子化済み係数からトークン列への変換
 * 
 * JPEGライクなRLEだが、並列デコード向けに設計:
 * - RUN長の上限を15に制限（固定アルファベット）
 * - MAGCはクラス分け（追加ビットで正確な値を指定）
 */
class Tokenizer {
public:
    /**
     * 係数ブロック（64個）をトークン化
     * 
     * @param coeffs スキャン順の係数列（64個）
     * @return トークン列
     */
    static std::vector<Token> tokenize_block(const int16_t* coeffs, int size = 64) {
        std::vector<Token> tokens;
        tokens.reserve(size);  // worst case
        
        int i = 0;
        while (i < size) {
            // ゼロラン検出
            int run = 0;
            while (i + run < size && coeffs[i + run] == 0) {
                run++;
            }
            
            // EOB判定（残り全部0）
            if (i + run == size) {
                tokens.emplace_back(TokenType::EOB);
                break;
            }
            
            // RUN トークン
            while (run >= 16) {
                tokens.emplace_back(TokenType::RUN_ESC, run - 16, 4);  // 4bit で [0, 15] を指定
                run = 0;
                i += 16;
            }
            if (run > 0) {
                tokens.emplace_back(static_cast<TokenType>(
                    static_cast<int>(TokenType::RUN_0) + run));
            }
            i += run;
            
            // 非ゼロ係数
            if (i < size) {
                int16_t v = coeffs[i];
                uint16_t abs_v = std::abs(v);
                
                // MAGC クラス
                int magc = (abs_v == 0) ? 0 : (32 - __builtin_clz(abs_v));  // floor(log2(abs_v)) + 1
                magc = std::min(magc, 11);
                tokens.emplace_back(static_cast<TokenType>(
                    static_cast<int>(TokenType::MAGC_0) + magc));
                
                // 符号
                tokens.emplace_back((v > 0) ? TokenType::SIGN_POS : TokenType::SIGN_NEG);
                
                // REM（下位ビット）
                if (magc > 0) {
                    uint16_t rem = abs_v - (1 << (magc - 1));
                    tokens.emplace_back(TokenType::MAGC_0, rem, magc - 1);  // type は使わない（raw bits のみ）
                }
                
                i++;
            }
        }
        
        return tokens;
    }

    /**
     * トークン列から係数ブロックを復元
     */
    static std::vector<int16_t> detokenize_block(const std::vector<Token>& tokens, int size = 64) {
        std::vector<int16_t> coeffs(size, 0);
        
        int pos = 0;
        size_t token_idx = 0;
        
        while (token_idx < tokens.size() && pos < size) {
            const Token& tok = tokens[token_idx++];
            
            // EOB
            if (tok.type == TokenType::EOB) {
                break;
            }
            
            // RUN
            if (tok.type >= TokenType::RUN_0 && tok.type <= TokenType::RUN_15) {
                int run = static_cast<int>(tok.type) - static_cast<int>(TokenType::RUN_0);
                pos += run;
            } else if (tok.type == TokenType::RUN_ESC) {
                pos += 16 + tok.raw_bits;
            }
            
            // MAGC
            if (tok.type >= TokenType::MAGC_0 && tok.type <= TokenType::MAGC_11) {
                int magc = static_cast<int>(tok.type) - static_cast<int>(TokenType::MAGC_0);
                
                // 符号
                if (token_idx >= tokens.size()) break;
                const Token& sign_tok = tokens[token_idx++];
                int sign = (sign_tok.type == TokenType::SIGN_POS) ? 1 : -1;
                
                // REM
                uint16_t abs_v = 0;
                if (magc > 0) {
                    if (token_idx >= tokens.size()) break;
                    const Token& rem_tok = tokens[token_idx++];
                    abs_v = (1 << (magc - 1)) + rem_tok.raw_bits;
                }
                
                if (pos < size) {
                    coeffs[pos++] = sign * abs_v;
                }
            }
        }
        
        return coeffs;
    }
};

} // namespace hakonyans
