namespace detail {

#include "lossless_natural_route_lz_impl.h"

template <typename ZigzagEncodeFn, typename EncodeSharedLzFn>
inline std::vector<uint8_t> build_mode0_payload(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val, EncodeSharedLzFn&& encode_byte_stream_shared_lz
) {
    std::vector<uint8_t> row_pred_ids(pad_h, 0);
    std::vector<uint8_t> residual_bytes;
    residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = residual_bytes.data();

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int up = up_row ? (int)up_row[x] : 0;
            cost0 += (uint64_t)std::abs(cur);
            cost1 += (uint64_t)std::abs(cur - up);
            cost2 += (uint64_t)std::abs(cur - (up / 2));
        }

        int best_p = 0;
        uint64_t best_cost = cost0;
        if (cost1 < best_cost) {
            best_cost = cost1;
            best_p = 1;
        }
        if (cost2 < best_cost) {
            best_p = 2;
        }
        row_pred_ids[y] = (uint8_t)best_p;

        if (best_p == 0) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t left = (x > 0) ? row[x - 1] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)left);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else if (best_p == 1) {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t up = up_row ? up_row[x] : 0;
                const int16_t resid = (int16_t)((int)row[x] - (int)up);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        } else {
            for (uint32_t x = 0; x < pad_w; x++) {
                const int16_t left = (x > 0) ? row[x - 1] : 0;
                const int16_t up = up_row ? up_row[x] : 0;
                const int16_t pred = (int16_t)(((int)left + (int)up) / 2);
                const int16_t resid = (int16_t)((int)row[x] - (int)pred);
                const uint16_t zz = zigzag_encode_val(resid);
                resid_dst[0] = (uint8_t)(zz & 0xFF);
                resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
                resid_dst += 2;
            }
        }
    }

    auto resid_lz = TileLZ::compress(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    // [magic][mode=0][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4][pred_ids][payload]
    std::vector<uint8_t> out;
    out.reserve(18 + row_pred_ids.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(0);
    uint32_t pred_count = pad_h;
    uint32_t resid_raw_count = (uint32_t)residual_bytes.size();
    uint32_t resid_payload_size = (uint32_t)resid_lz_rans.size();
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pred_count);
    push_u32(resid_raw_count);
    push_u32(resid_payload_size);
    out.insert(out.end(), row_pred_ids.begin(), row_pred_ids.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

struct Mode1Prepared {
    std::vector<uint8_t> row_pred_ids;
    std::vector<int16_t> residuals;
    std::vector<uint8_t> residual_bytes;
};

struct PackedPredictorStream {
    uint8_t mode = 0; // 0=raw, 1=rANS
    std::vector<uint8_t> payload;
    bool valid = false;
};

inline size_t mode12_min_candidate_size(const PackedPredictorStream& packed_pred) {
    if (!packed_pred.valid || packed_pred.payload.empty()) {
        return std::numeric_limits<size_t>::max();
    }
    // mode1/mode2 wrapper fixed header (27 bytes) + pred payload + residual payload (>=1 byte)
    return 27u + packed_pred.payload.size() + 1u;
}

template <typename EncodeByteStreamFn>
inline PackedPredictorStream build_packed_predictor_stream(
    const std::vector<uint8_t>& row_pred_ids,
    EncodeByteStreamFn&& encode_byte_stream
) {
    PackedPredictorStream out;
    if (row_pred_ids.empty()) return out;

    out.payload = row_pred_ids;
    auto pred_rans = encode_byte_stream(row_pred_ids);
    if (!pred_rans.empty() && pred_rans.size() < out.payload.size()) {
        out.payload = std::move(pred_rans);
        out.mode = 1;
    }
    out.valid = true;
    return out;
}

template <typename ZigzagEncodeFn>
inline Mode1Prepared build_mode1_prepared(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    ZigzagEncodeFn&& zigzag_encode_val
) {
    Mode1Prepared prepared;
    prepared.row_pred_ids.resize(pad_h, 0);
    prepared.residuals.resize(pixel_count);
    prepared.residual_bytes.resize((size_t)pixel_count * 2);
    uint8_t* resid_dst = prepared.residual_bytes.data();

    std::vector<int16_t> recon(pixel_count, 0);

    for (uint32_t y = 0; y < pad_h; y++) {
        const int16_t* row = padded + (size_t)y * pad_w;
        const int16_t* up_row = (y > 0) ? (padded + (size_t)(y - 1) * pad_w) : nullptr;

        uint64_t cost0 = 0; // SUB (left=0 in current cost evaluation semantics)
        uint64_t cost1 = 0; // UP
        uint64_t cost2 = 0; // AVG(left=0,up)
        uint64_t cost3 = 0; // PAETH
        uint64_t cost4 = 0; // MED
        uint64_t cost5 = 0; // WEIGHTED_A
        uint64_t cost6 = 0; // WEIGHTED_B
        for (uint32_t x = 0; x < pad_w; x++) {
            const int cur = (int)row[x];
            const int16_t b = up_row ? up_row[x] : 0;
            const int16_t c = (up_row && x > 0) ? up_row[x - 1] : 0;
            const int16_t a = (x > 0) ? row[x - 1] : 0;
            const int pred2 = ((int)a + (int)b) / 2;
            const int pred3 = (int)LosslessFilter::paeth_predictor(a, b, c);
            const int pred4 = (int)LosslessFilter::med_predictor(a, b, c);
            const int pred5 = ((int)a * 3 + (int)b) / 4;
            const int pred6 = ((int)a + (int)b * 3) / 4;
            cost0 += (uint64_t)std::abs(cur - (int)a);
            cost1 += (uint64_t)std::abs(cur - (int)b);
            cost2 += (uint64_t)std::abs(cur - pred2);
            cost3 += (uint64_t)std::abs(cur - pred3);
            cost4 += (uint64_t)std::abs(cur - pred4);
            cost5 += (uint64_t)std::abs(cur - pred5);
            cost6 += (uint64_t)std::abs(cur - pred6);
        }

        int best_p = 0;
        uint64_t best_cost = cost0;
        if (cost1 < best_cost) {
            best_cost = cost1;
            best_p = 1;
        }
        if (cost2 < best_cost) {
            best_cost = cost2;
            best_p = 2;
        }
        if (cost3 < best_cost) {
            best_cost = cost3;
            best_p = 3;
        }
        if (cost4 < best_cost) {
            best_cost = cost4;
            best_p = 4;
        }
        if (cost5 < best_cost) {
            best_cost = cost5;
            best_p = 5;
        }
        if (cost6 < best_cost) {
            best_p = 6;
        }
        prepared.row_pred_ids[y] = (uint8_t)best_p;

        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (best_p == 0) pred = a;
            else if (best_p == 1) pred = b;
            else if (best_p == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (best_p == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else if (best_p == 4) pred = LosslessFilter::med_predictor(a, b, c);
            else if (best_p == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
            else pred = (int16_t)(((int)a + (int)b * 3) / 4);

            int16_t cur = row[x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);
            prepared.residuals[(size_t)y * pad_w + x] = resid;

            uint16_t zz = zigzag_encode_val(resid);
            resid_dst[0] = (uint8_t)(zz & 0xFF);
            resid_dst[1] = (uint8_t)((zz >> 8) & 0xFF);
            resid_dst += 2;
        }
    }
    return prepared;
}

template <typename Mode1PreparedT,
          typename EncodeSharedLzFn,
          typename CompressResidualFn>
inline std::vector<uint8_t> build_mode1_payload_from_prepared(
    const Mode1PreparedT& prepared,
    const PackedPredictorStream& packed_pred,
    uint32_t pad_h,
    uint32_t pixel_count,
    EncodeSharedLzFn&& encode_byte_stream_shared_lz,
    uint8_t out_mode,
    CompressResidualFn&& compress_residual
) {
    const auto& residual_bytes = prepared.residual_bytes;
    if (!packed_pred.valid || packed_pred.payload.empty() || residual_bytes.empty()) return {};

    auto resid_lz = compress_residual(residual_bytes);
    if (resid_lz.empty()) return {};
    auto resid_lz_rans = encode_byte_stream_shared_lz(resid_lz);
    if (resid_lz_rans.empty()) return {};

    // [magic][mode=1/2][pixel_count:4][pred_count:4][resid_raw_count:4][resid_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][resid_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + packed_pred.payload.size() + resid_lz_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(out_mode);
    uint32_t pred_count = pad_h;
    uint32_t resid_raw_count = (uint32_t)residual_bytes.size();
    uint32_t resid_payload_size = (uint32_t)resid_lz_rans.size();
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pred_count);
    push_u32(resid_raw_count);
    push_u32(resid_payload_size);
    out.push_back(packed_pred.mode);
    push_u32(pred_count);
    push_u32((uint32_t)packed_pred.payload.size());
    out.insert(out.end(), packed_pred.payload.begin(), packed_pred.payload.end());
    out.insert(out.end(), resid_lz_rans.begin(), resid_lz_rans.end());
    return out;
}

template <typename Mode1PreparedT,
          typename PackedPredictorStreamT,
          typename EncodeByteStreamFn>
inline std::vector<uint8_t> build_mode3_payload_from_prepared(
    const int16_t* padded, uint32_t pad_w, uint32_t pad_h, uint32_t pixel_count,
    const Mode1PreparedT& prepared,
    const PackedPredictorStreamT& packed_pred,
    EncodeByteStreamFn&& encode_byte_stream
) {
    if (!packed_pred.valid || packed_pred.payload.empty()) return {};

    std::vector<uint8_t> flat_bytes;
    std::vector<uint8_t> edge_bytes;
    flat_bytes.reserve((size_t)pixel_count * 2);
    edge_bytes.reserve((size_t)pixel_count * 2);

    const std::vector<uint8_t>& pred_ids = prepared.row_pred_ids;
    std::vector<int16_t> recon(pixel_count, 0);

    for (uint32_t y = 0; y < pad_h; y++) {
        uint8_t pid = pred_ids[y];
        const int16_t* padded_row = padded + (size_t)y * pad_w;
        for (uint32_t x = 0; x < pad_w; x++) {
            int16_t a = (x > 0) ? recon[(size_t)y * pad_w + (x - 1)] : 0;
            int16_t b = (y > 0) ? recon[(size_t)(y - 1) * pad_w + x] : 0;
            int16_t c = (x > 0 && y > 0) ? recon[(size_t)(y - 1) * pad_w + (x - 1)] : 0;
            int16_t pred = 0;
            if (pid == 0) pred = a;
            else if (pid == 1) pred = b;
            else if (pid == 2) pred = (int16_t)(((int)a + (int)b) / 2);
            else if (pid == 3) pred = LosslessFilter::paeth_predictor(a, b, c);
            else if (pid == 4) pred = LosslessFilter::med_predictor(a, b, c);
            else if (pid == 5) pred = (int16_t)(((int)a * 3 + (int)b) / 4);
            else if (pid == 6) pred = (int16_t)(((int)a + (int)b * 3) / 4);

            int16_t cur = padded_row[x];
            int16_t resid = (int16_t)(cur - pred);
            recon[(size_t)y * pad_w + x] = (int16_t)(pred + resid);

            uint16_t zz = zigzag_encode_val(resid);
            
            int grad = std::max(std::abs(a - c), std::abs(b - c));
            if (grad < 16) {
                flat_bytes.push_back((uint8_t)(zz & 0xFF));
                flat_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
            } else {
                edge_bytes.push_back((uint8_t)(zz & 0xFF));
                edge_bytes.push_back((uint8_t)((zz >> 8) & 0xFF));
            }
        }
    }

    auto flat_rans = encode_byte_stream(flat_bytes);
    auto edge_rans = encode_byte_stream(edge_bytes);

    // [magic][mode=3][pixel_count:4][pred_count:4][flat_payload_size:4][edge_payload_size:4]
    // [pred_mode:1][pred_raw_count:4][pred_payload_size:4][pred_payload][flat_payload][edge_payload]
    std::vector<uint8_t> out;
    out.reserve(27 + packed_pred.payload.size() + flat_rans.size() + edge_rans.size());
    out.push_back(FileHeader::WRAPPER_MAGIC_NATURAL_ROW);
    out.push_back(3);
    auto push_u32 = [&](uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    push_u32(pixel_count);
    push_u32(pad_h); // pred_count
    push_u32((uint32_t)flat_rans.size());
    push_u32((uint32_t)edge_rans.size());
    out.push_back(packed_pred.mode);
    push_u32(pad_h); // pred_raw_count
    push_u32((uint32_t)packed_pred.payload.size());
    out.insert(out.end(), packed_pred.payload.begin(), packed_pred.payload.end());
    out.insert(out.end(), flat_rans.begin(), flat_rans.end());
    out.insert(out.end(), edge_rans.begin(), edge_rans.end());
    return out;
}

} // namespace detail
