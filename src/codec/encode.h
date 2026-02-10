#pragma once

#include "headers.h"
#include "transform_dct.h"
#include "quant.h"
#include "zigzag.h"
#include "colorspace.h"
#include "../entropy/nyans_p/tokenization_v2.h"
#include "../entropy/nyans_p/rans_flat_interleaved.h"
#include "../entropy/nyans_p/rans_tables.h"
#include "../entropy/nyans_p/pindex.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace hakonyans {

class GrayscaleEncoder {
public:
    static std::vector<uint8_t> encode(const uint8_t* pixels, uint32_t width, uint32_t height, uint8_t quality = 75) {
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 1; header.colorspace = 2; header.subsampling = 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        uint32_t pad_w = header.padded_width(), pad_h = header.padded_height();
        uint16_t quant[64]; QuantTable::build_quant_table(quality, quant);
        auto tile_data = encode_plane(pixels, width, height, pad_w, pad_h, quant, true, true);
        QMATChunk qmat; qmat.quality = quality; qmat.num_tables = 1; std::memcpy(qmat.quant_y, quant, 128);
        auto qmat_data = qmat.serialize();
        ChunkDirectory dir; dir.add("QMAT", 0, qmat_data.size()); dir.add("TIL0", 0, tile_data.size());
        auto dir_data = dir.serialize();
        size_t qmat_offset = 48 + dir_data.size();
        size_t tile_offset = qmat_offset + qmat_data.size();
        dir.entries[0].offset = qmat_offset; dir.entries[1].offset = tile_offset;
        dir_data = dir.serialize();
        std::vector<uint8_t> output; output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end());
        output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_data.begin(), tile_data.end());
        return output;
    }

    static std::vector<uint8_t> encode_color(const uint8_t* rgb_data, uint32_t width, uint32_t height, uint8_t quality = 75, bool use_420 = true, bool use_cfl = true) {
        std::vector<uint8_t> y_plane(width * height), cb_plane(width * height), cr_plane(width * height);
        for (uint32_t i = 0; i < width * height; i++) rgb_to_ycbcr(rgb_data[i*3], rgb_data[i*3+1], rgb_data[i*3+2], y_plane[i], cb_plane[i], cr_plane[i]);
        FileHeader header; header.width = width; header.height = height; header.bit_depth = 8;
        header.num_channels = 3; header.colorspace = 0; header.subsampling = use_420 ? 1 : 0;
        header.tile_cols = 1; header.tile_rows = 1; header.quality = quality; header.pindex_density = 2;
        if (use_cfl) header.flags |= 2;
        uint16_t quant[64]; QuantTable::build_quant_table(quality, quant);
        uint32_t pad_w_y = header.padded_width(), pad_h_y = header.padded_height();
        auto tile_y = encode_plane(y_plane.data(), width, height, pad_w_y, pad_h_y, quant, true, true);
        std::vector<uint8_t> tile_cb, tile_cr;
        if (use_420) {
            int cb_w, cb_h; std::vector<uint8_t> cb_420, cr_420, y_ds;
            downsample_420(cb_plane.data(), width, height, cb_420, cb_w, cb_h);
            downsample_420(cr_plane.data(), width, height, cr_420, cb_w, cb_h);
            uint32_t pad_w_c = ((cb_w + 7) / 8) * 8, pad_h_c = ((cb_h + 7) / 8) * 8;
            if (use_cfl) { downsample_420(y_plane.data(), width, height, y_ds, cb_w, cb_h); }
            tile_cb = encode_plane(cb_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, quant, true, true, use_cfl ? &y_ds : nullptr, 0);
            tile_cr = encode_plane(cr_420.data(), cb_w, cb_h, pad_w_c, pad_h_c, quant, true, true, use_cfl ? &y_ds : nullptr, 1);
        } else {
            tile_cb = encode_plane(cb_plane.data(), width, height, pad_w_y, pad_h_y, quant, true, true, use_cfl ? &y_plane : nullptr, 0);
            tile_cr = encode_plane(cr_plane.data(), width, height, pad_w_y, pad_h_y, quant, true, true, use_cfl ? &y_plane : nullptr, 1);
        }
        QMATChunk qmat; qmat.quality = quality; std::memcpy(qmat.quant_y, quant, 128); auto qmat_data = qmat.serialize();
        ChunkDirectory dir; dir.add("QMAT", 0, qmat_data.size()); dir.add("TIL0", 0, tile_y.size()); dir.add("TIL1", 0, tile_cb.size()); dir.add("TIL2", 0, tile_cr.size());
        auto dir_data = dir.serialize(); size_t offset = 48 + dir_data.size();
        for (int i = 0; i < 4; i++) { dir.entries[i].offset = offset; offset += (i==0?qmat_data.size():(i==1?tile_y.size():(i==2?tile_cb.size():tile_cr.size()))); }
        dir_data = dir.serialize();
        std::vector<uint8_t> output; output.resize(48); header.write(output.data());
        output.insert(output.end(), dir_data.begin(), dir_data.end()); output.insert(output.end(), qmat_data.begin(), qmat_data.end());
        output.insert(output.end(), tile_y.begin(), tile_y.end()); output.insert(output.end(), tile_cb.begin(), tile_cb.end()); output.insert(output.end(), tile_cr.begin(), tile_cr.end());
        return output;
    }

private:
    static std::vector<uint8_t> encode_plane(
        const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h,
        const uint16_t quant[64], bool pi=false, bool aq=false, const std::vector<uint8_t>* y_ref=nullptr, int chroma_idx=0
    ) {
        std::vector<uint8_t> padded = pad_image(pixels, width, height, pad_w, pad_h);
        std::vector<uint8_t> y_padded; if (y_ref) y_padded = pad_image(y_ref->data(), (y_ref->size() > width*height/2 ? width : (width+1)/2), (y_ref->size() > width*height/2 ? height : (height+1)/2), pad_w, pad_h);
        int nx = pad_w / 8, ny = pad_h / 8, nb = nx * ny;
        std::vector<std::vector<int16_t>> dct_blocks(nb, std::vector<int16_t>(64));
        std::vector<float> activities(nb); float total_activity = 0.0f;
        std::vector<CfLParams> cfl_params;
        for (int i = 0; i < nb; i++) {
            int bx = i % nx, by = i / nx; int16_t block[64]; extract_block(padded.data(), pad_w, pad_h, bx, by, block);
            if (y_ref) {
                int16_t yb[64]; extract_block(y_padded.data(), pad_w, pad_h, bx, by, yb);
                uint8_t yu[64], cu[64]; for (int k=0; k<64; k++) { yu[k]=(uint8_t)(yb[k]+128); cu[k]=(uint8_t)(block[k]+128); }
                CfLParams p = compute_cfl_params(yu, cu, cu); float a = (chroma_idx==0?p.alpha_cb:p.alpha_cr), b = (chroma_idx==0?p.beta_cb:p.beta_cr);
                cfl_params.push_back({a, b, a, b});
                for (int k=0; k<64; k++) block[k] = (int16_t)std::clamp((int)cu[k] - (int)std::round(a*yu[k]+b), -128, 127);
            }
            int16_t dct_out[64], zigzag[64]; DCT::forward(block, dct_out); Zigzag::scan(dct_out, zigzag);
            std::memcpy(dct_blocks[i].data(), zigzag, 128);
            if (aq) { float act = QuantTable::calc_activity(&zigzag[1]); activities[i] = act; total_activity += act; }
        }
        float avg_activity = total_activity / nb;
        std::vector<Token> dc_tokens; std::vector<Token> ac_tokens;
        std::vector<int8_t> q_deltas; if (aq) q_deltas.reserve(nb);
        int16_t prev_dc = 0;
        for (int i = 0; i < nb; i++) {
            float scale = 1.0f; if (aq) { scale = QuantTable::get_adaptive_scale(activities[i], avg_activity); int8_t delta = (int8_t)std::clamp((scale - 1.0f) * 50.0f, -127.0f, 127.0f); q_deltas.push_back(delta); scale = 1.0f + (delta / 50.0f); }
            int16_t quantized[64]; for (int k = 0; k < 64; k++) { int16_t coeff = dct_blocks[i][k]; uint16_t q_adj = std::max((uint16_t)1, (uint16_t)std::round(quant[k] * scale)); int sign = (coeff < 0) ? -1 : 1; quantized[k] = sign * ((std::abs(coeff) + q_adj / 2) / q_adj); }
            int16_t dc_diff = quantized[0] - prev_dc; prev_dc = quantized[0]; dc_tokens.push_back(Tokenizer::tokenize_dc(dc_diff));
            auto at = Tokenizer::tokenize_ac(&quantized[1]); ac_tokens.insert(ac_tokens.end(), at.begin(), at.end());
        }
        std::vector<uint8_t> pindex_data;
        auto dc_stream = encode_tokens(dc_tokens, build_cdf(dc_tokens));
        auto ac_stream = encode_tokens(ac_tokens, build_cdf(ac_tokens), pi ? &pindex_data : nullptr);
        std::vector<uint8_t> tile_data;
        uint32_t sz[5] = {(uint32_t)dc_stream.size(), (uint32_t)ac_stream.size(), (uint32_t)pindex_data.size(), (uint32_t)q_deltas.size(), (uint32_t)cfl_params.size()*2};
        tile_data.resize(20); std::memcpy(&tile_data[0], sz, 20);
        tile_data.insert(tile_data.end(), dc_stream.begin(), dc_stream.end()); tile_data.insert(tile_data.end(), ac_stream.begin(), ac_stream.end());
        if (sz[2]>0) tile_data.insert(tile_data.end(), pindex_data.begin(), pindex_data.end());
        if (sz[3]>0) { const uint8_t* p = reinterpret_cast<const uint8_t*>(q_deltas.data()); tile_data.insert(tile_data.end(), p, p + sz[3]); }
        if (sz[4]>0) { for (const auto& p : cfl_params) { tile_data.push_back((int8_t)std::clamp(p.alpha_cb * 64.0f, -128.0f, 127.0f)); tile_data.push_back((uint8_t)std::clamp(p.beta_cb, 0.0f, 255.0f)); } }
        return tile_data;
    }

    static CDFTable build_cdf(const std::vector<Token>& t) { std::vector<uint32_t> f(76, 1); for (const auto& x : t) { int sym = static_cast<int>(x.type); if (sym < 76) f[sym]++; } return CDFBuilder().build_from_freq(f); }
    static std::vector<uint8_t> encode_tokens(const std::vector<Token>& t, const CDFTable& c, std::vector<uint8_t>* out_pi = nullptr) {
        std::vector<uint8_t> output; int alpha = c.alphabet_size; std::vector<uint8_t> cdf_data(alpha * 4);
        for (int i = 0; i < alpha; i++) { uint32_t f = c.freq[i]; std::memcpy(&cdf_data[i * 4], &f, 4); }
        uint32_t cdf_size = cdf_data.size(); output.resize(4); std::memcpy(output.data(), &cdf_size, 4); output.insert(output.end(), cdf_data.begin(), cdf_data.end());
        uint32_t token_count = t.size(); size_t count_offset = output.size(); output.resize(count_offset + 4); std::memcpy(&output[count_offset], &token_count, 4);
        FlatInterleavedEncoder encoder; for (const auto& tok : t) encoder.encode_symbol(c, static_cast<uint8_t>(tok.type)); auto rb = encoder.finish();
        if (out_pi) { auto pindex = PIndexBuilder::build(rb, c, t.size(), 1024); *out_pi = PIndexCodec::serialize(pindex); }
        uint32_t rans_size = rb.size(); size_t rs_offset = output.size(); output.resize(rs_offset + 4); std::memcpy(&output[rs_offset], &rans_size, 4); output.insert(output.end(), rb.begin(), rb.end());
        std::vector<uint8_t> raw_data; uint32_t raw_count = 0;
        for (const auto& tok : t) if (tok.raw_bits_count > 0) { raw_data.push_back(tok.raw_bits_count); raw_data.push_back(tok.raw_bits & 0xFF); raw_data.push_back((tok.raw_bits >> 8) & 0xFF); raw_count++; }
        size_t rc_offset = output.size(); output.resize(rc_offset + 4); std::memcpy(&output[rc_offset], &raw_count, 4); output.insert(output.end(), raw_data.begin(), raw_data.end());
        return output;
    }

    static std::vector<uint8_t> pad_image(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pad_w, uint32_t pad_h) {
        std::vector<uint8_t> padded(pad_w * pad_h);
        for (uint32_t y = 0; y < pad_h; y++) for (uint32_t x = 0; x < pad_w; x++) padded[y * pad_w + x] = pixels[std::min(y, height - 1) * width + std::min(x, width - 1)];
        return padded;
    }
    static void extract_block(const uint8_t* pixels, uint32_t stride, uint32_t height, int bx, int by, int16_t block[64]) {
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) block[y * 8 + x] = static_cast<int16_t>(pixels[(by * 8 + y) * stride + (bx * 8 + x)]) - 128;
    }
};

} // namespace hakonyans
