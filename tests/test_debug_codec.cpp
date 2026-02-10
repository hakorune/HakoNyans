#include "../src/codec/encode.h"
#include "../src/codec/decode.h"
#include <iostream>
#include <cmath>

using namespace hakonyans;

int main() {
    uint8_t img[256];
    for (int i = 0; i < 256; i++) img[i] = (i*7+i/16*3)%256;
    
    auto hkn = GrayscaleEncoder::encode(img, 16, 16, 75);
    
    // Parse
    FileHeader hdr = FileHeader::read(hkn.data());
    ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
    const ChunkEntry* qmat_entry = dir.find("QMAT");
    const ChunkEntry* tile_entry = dir.find("TILE");
    
    QMATChunk qmat = QMATChunk::deserialize(&hkn[qmat_entry->offset], qmat_entry->size);
    uint16_t deq[64];
    std::memcpy(deq, qmat.quant_y, 128);
    
    const uint8_t* td = &hkn[tile_entry->offset];
    uint32_t dcs, acs, pis;
    std::memcpy(&dcs, td, 4); std::memcpy(&acs, td+4, 4); std::memcpy(&pis, td+8, 4);
    
    // Inline decode_stream
    auto decode_stream = [](const uint8_t* stream, size_t size) -> std::vector<Token> {
        size_t offset = 0;
        uint32_t cdf_size;
        std::memcpy(&cdf_size, &stream[offset], 4); offset += 4;
        int alpha = cdf_size / 4;
        std::vector<uint32_t> fv(alpha);
        for (int i = 0; i < alpha; i++)
            std::memcpy(&fv[i], &stream[offset+i*4], 4);
        offset += cdf_size;
        CDFTable cdf = CDFBuilder::build_from_freq(fv);
        uint32_t tc; std::memcpy(&tc, &stream[offset], 4); offset += 4;
        uint32_t rs; std::memcpy(&rs, &stream[offset], 4); offset += 4;
        std::span<const uint8_t> rspan(&stream[offset], rs);
        FlatInterleavedDecoder<8> dec(rspan);
        std::vector<Token> tokens;
        for (uint32_t i = 0; i < tc; i++) {
            uint8_t sym = dec.decode_symbol(cdf);
            tokens.emplace_back(static_cast<TokenType>(sym), 0, 0);
        }
        offset += rs;
        uint32_t rc; std::memcpy(&rc, &stream[offset], 4); offset += 4;
        size_t ri = 0;
        for (auto& tok : tokens) {
            if (tok.type >= TokenType::MAGC_0 && tok.type <= TokenType::MAGC_11) {
                int magc = static_cast<int>(tok.type) - static_cast<int>(TokenType::MAGC_0);
                if (magc > 0 && ri < rc) {
                    tok.raw_bits_count = stream[offset];
                    tok.raw_bits = stream[offset+1] | (stream[offset+2] << 8);
                    offset += 3;
                    ri++;
                }
            }
        }
        CDFBuilder::cleanup(cdf);
        return tokens;
    };
    
    auto dc_tokens = decode_stream(td+12, dcs);
    auto ac_tokens = decode_stream(td+12+dcs, acs);
    
    std::cout << "DC tokens: " << dc_tokens.size() << ", AC tokens: " << ac_tokens.size() << std::endl;
    
    // Encoder-side reference
    uint16_t eq[64];
    QuantTable::build_quant_table(75, eq);
    
    size_t dc_idx = 0, ac_idx = 0;
    int16_t prev_dc_dec = 0, prev_dc_enc = 0;
    
    for (int by = 0; by < 2; by++) {
        for (int bx = 0; bx < 2; bx++) {
            // Encoder reference
            int16_t eblock[64];
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    eblock[y*8+x] = (int16_t)img[(by*8+y)*16+(bx*8+x)] - 128;
            int16_t edct[64], ezz[64], eqz[64];
            DCT::forward(eblock, edct);
            Zigzag::scan(edct, ezz);
            QuantTable::quantize(ezz, eq, eqz);
            int16_t edc_diff = eqz[0] - prev_dc_enc;
            prev_dc_enc = eqz[0];
            
            // Decoder side
            int16_t dc_diff = Tokenizer::detokenize_dc(dc_tokens[dc_idx++]);
            int16_t dc = prev_dc_dec + dc_diff;
            prev_dc_dec = dc;
            
            // Collect AC tokens
            std::vector<Token> block_ac;
            while (ac_idx < ac_tokens.size()) {
                Token tok = ac_tokens[ac_idx++];
                block_ac.push_back(tok);
                if (static_cast<int>(tok.type) == 63) break;
                if (static_cast<int>(tok.type) < 64) {
                    if (ac_idx < ac_tokens.size())
                        block_ac.push_back(ac_tokens[ac_idx++]);
                }
            }
            
            int16_t ac[63];
            Tokenizer::detokenize_ac(block_ac, ac);
            
            // Compare
            bool dc_ok = (dc == eqz[0]);
            int ac_errs = 0;
            for (int i = 0; i < 63; i++)
                if (ac[i] != eqz[i+1]) ac_errs++;
            
            std::cout << "Block(" << bx << "," << by << "): DC expected=" << eqz[0] 
                      << " got=" << dc << (dc_ok ? " OK" : " ERR")
                      << " AC_errs=" << ac_errs << "/63"
                      << " block_ac=" << block_ac.size() << std::endl;
            
            if (!dc_ok) {
                std::cout << "  dc_diff expected=" << edc_diff << " got=" << dc_diff << std::endl;
            }
            if (ac_errs > 0 && ac_errs <= 5) {
                for (int i = 0; i < 63; i++) {
                    if (ac[i] != eqz[i+1])
                        std::cout << "  AC[" << i << "]: exp=" << eqz[i+1] << " got=" << ac[i] << std::endl;
                }
            }
        }
    }
    std::cout << "AC consumed: " << ac_idx << "/" << ac_tokens.size() << std::endl;
    
    return 0;
}
