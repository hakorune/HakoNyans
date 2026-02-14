// ============================================================
void test_filter_lo_mode6_v17_typebit_lensplit() {
    TEST("Mode6 v0x0017 type bitpack + len split decode");

    // Build a v0x0017 Mode 6 payload manually
    // Token stream: 2 LITRUN + 1 MATCH
    // LITRUN(0): len=2, data=[0xAB, 0xCD]
    // LITRUN(1): len=1, data=[0xEF]
    // MATCH(2): len=5, dist=0x1234

    const uint32_t raw_count = 8; // 2+1+5 = 8 bytes
    const uint32_t token_count = 3;
    const uint32_t match_count = 1;
    const uint32_t lit_token_count = 2;

    // Verify pre-check: token_count == lit_token_count + match_count
    if (token_count != lit_token_count + match_count) {
        FAIL("Token count consistency check failed");
        return;
    }

    // Build v17 streams
    // type_bits: packed bits (0=LITRUN, 1=MATCH)
    // 2 LITRUN + 1 MATCH = bits: 0,0,1 = 0b100 = 0x04 (LSB first)
    std::vector<uint8_t> type_bits = {0x04}; // ceil(3/8) = 1 byte
    std::vector<uint8_t> lit_len = {2, 1};      // LIT token lengths only
    std::vector<uint8_t> match_len = {5};       // MATCH token lengths only
    std::vector<uint8_t> dist_lo = {0x34};      // MATCH distances only
    std::vector<uint8_t> dist_hi = {0x12};
    std::vector<uint8_t> lit_stream = {0xAB, 0xCD, 0xEF};

    // Build payload header (v0x0017 format)
    std::vector<uint8_t> payload;
    payload.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
    payload.push_back(6); // mode=6
    payload.push_back((uint8_t)(raw_count & 0xFF));
    payload.push_back((uint8_t)((raw_count >> 8) & 0xFF));
    payload.push_back((uint8_t)((raw_count >> 16) & 0xFF));
    payload.push_back((uint8_t)((raw_count >> 24) & 0xFF));

    auto push_u32 = [&payload](uint32_t val) {
        payload.push_back((uint8_t)(val & 0xFF));
        payload.push_back((uint8_t)((val >> 8) & 0xFF));
        payload.push_back((uint8_t)((val >> 16) & 0xFF));
        payload.push_back((uint8_t)((val >> 24) & 0xFF));
    };

    // v17: token_count + match_count + lit_token_count
    push_u32(token_count);
    push_u32(match_count);
    push_u32(lit_token_count);

    // v17 stream sizes: type_bits + lit_len + match_len + dist_lo + dist_hi + lit
    push_u32(type_bits.size());
    push_u32(lit_len.size());
    push_u32(match_len.size());
    push_u32(dist_lo.size());
    push_u32(dist_hi.size());
    push_u32(lit_stream.size());

    // Insert streams
    payload.insert(payload.end(), type_bits.begin(), type_bits.end());
    payload.insert(payload.end(), lit_len.begin(), lit_len.end());
    payload.insert(payload.end(), match_len.begin(), match_len.end());
    payload.insert(payload.end(), dist_lo.begin(), dist_lo.end());
    payload.insert(payload.end(), dist_hi.begin(), dist_hi.end());
    payload.insert(payload.end(), lit_stream.begin(), lit_stream.end());

    if (payload.size() < 36) {
        FAIL("v0x0017 payload too small");
        return;
    }

    // Mock decoders
    auto mock_decode = [](const uint8_t* data, size_t size, size_t) -> std::vector<uint8_t> {
        return std::vector<uint8_t>(data, data + size);
    };

    auto mock_decompress = [](const uint8_t* data, size_t size, size_t raw_count) -> std::vector<uint8_t> {
        std::vector<uint8_t> out;
        out.reserve(raw_count);
        size_t pos = 0;
        while (pos < size && out.size() < raw_count) {
            if (pos >= size) break;
            uint8_t tag = data[pos++];
            if (tag == 0) {
                if (pos >= size) break;
                uint8_t len = data[pos++];
                if (pos + len > size) break;
                out.insert(out.end(), data + pos, data + pos + len);
                pos += len;
            } else if (tag == 1) {
                if (pos >= size) break;
                uint8_t len = data[pos++];
                if (pos + 2 > size) break;
                uint16_t dist = data[pos] | (data[pos+1] << 8);
                pos += 2;
                if (dist == 0 || dist > out.size()) break;
                size_t start = out.size() - dist;
                for (int i = 0; i < len && out.size() < raw_count; i++) {
                    out.push_back(out[start + i]);
                }
            }
        }
        return out;
    };

    std::vector<uint8_t> filter_ids;
    std::vector<FileHeader::BlockType> block_types;

    auto result = lossless_filter_lo_decode::decode_filter_lo_stream(
        payload.data(),
        (uint32_t)payload.size(),
        raw_count,
        filter_ids,
        block_types,
        0, 0,
        false, true,
        FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS_V3, // v0x0017
        mock_decode, mock_decode, mock_decompress, nullptr
    );

    if (result.size() == raw_count) {
        PASS();
    } else {
        FAIL("Result size mismatch");
    }
}

// ============================================================
// LZCOST filter row selection tests (Phase 9X-3)
// ============================================================
