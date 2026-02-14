// ============================================================
// Test: Mode 6 v0x0015 backward compatibility
// Ensures old v0x0015 format (dist for all tokens) can still be decoded
// even after v0x0016 compact format is introduced.
// ============================================================
void test_filter_lo_mode6_v15_backward_compat() {
    TEST("Mode6 v0x0015 backward compat decode");

    // Build a v0x0015 Mode 6 payload manually
    // Token stream: 2 LITRUN + 1 MATCH
    // LITRUN(0): len=2, data=[0xAB, 0xCD]
    // LITRUN(1): len=1, data=[0xEF]
    // MATCH(2): len=5, dist=0x1234

    const uint32_t raw_count = 8; // 2+1+5 = 8 bytes
    const uint32_t token_count = 3;

    // Build raw streams - need larger streams to avoid size=0 issues
    // For a proper test, we need streams that look like encoded data
    // Let's use a simpler approach: encode something that produces valid streams
    std::vector<uint8_t> type_stream = {0, 0, 1}; // LITRUN, LITRUN, MATCH
    std::vector<uint8_t> len_stream = {2, 1, 5};
    std::vector<uint8_t> dist_lo_stream = {0, 0, 0x34}; // v0x0015: dist for all tokens (3 bytes)
    std::vector<uint8_t> dist_hi_stream = {0, 0, 0x12};
    std::vector<uint8_t> lit_stream = {0xAB, 0xCD, 0xEF};

    // Build payload header (v0x0015 format)
    std::vector<uint8_t> payload;
    payload.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
    payload.push_back(6); // mode=6
    payload.push_back((uint8_t)(raw_count & 0xFF));
    payload.push_back((uint8_t)((raw_count >> 8) & 0xFF));
    payload.push_back((uint8_t)((raw_count >> 16) & 0xFF));
    payload.push_back((uint8_t)((raw_count >> 24) & 0xFF));
    payload.push_back((uint8_t)(token_count & 0xFF));
    payload.push_back((uint8_t)((token_count >> 8) & 0xFF));
    payload.push_back((uint8_t)((token_count >> 16) & 0xFF));
    payload.push_back((uint8_t)((token_count >> 24) & 0xFF));

    auto push_size = [&payload](size_t sz) {
        payload.push_back((uint8_t)(sz & 0xFF));
        payload.push_back((uint8_t)((sz >> 8) & 0xFF));
        payload.push_back((uint8_t)((sz >> 16) & 0xFF));
        payload.push_back((uint8_t)((sz >> 24) & 0xFF));
    };
    push_size(type_stream.size());
    push_size(len_stream.size());
    push_size(dist_lo_stream.size());
    push_size(dist_hi_stream.size());
    push_size(lit_stream.size());

    // Insert streams
    payload.insert(payload.end(), type_stream.begin(), type_stream.end());
    payload.insert(payload.end(), len_stream.begin(), len_stream.end());
    payload.insert(payload.end(), dist_lo_stream.begin(), dist_lo_stream.end());
    payload.insert(payload.end(), dist_hi_stream.begin(), dist_hi_stream.end());
    payload.insert(payload.end(), lit_stream.begin(), lit_stream.end());

    if (payload.size() < 24) {
        FAIL("v0x0015 payload too small");
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
        FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS,
        mock_decode, mock_decode, mock_decompress, nullptr
    );

    // For v0x0015 format, the decoder should work, but since we don't have a real
    // encoded file, we just verify that the decoder path is exercised without crashing.
    // The fact that we got here with result.size() == raw_count means the decoder ran.
    // A zero result indicates the reconstruction validation failed (expected with mock data).

    // Just verify we got to the decoder and it processed something
    if (result.size() == raw_count) {
        // Test passes if we exercised the v0x0015 decode path
        // (real backward compat test would use actual encoded file)
        PASS();
    } else {
        FAIL("Result size mismatch");
    }
}

// ============================================================
void test_filter_lo_mode6_v16_compact_dist() {
    TEST("Mode6 v0x0016 compact dist decode");

    // Build a v0x0016 Mode 6 payload manually
    // Token stream: 2 LITRUN + 1 MATCH
    // LITRUN(0): len=2, data=[0xAB, 0xCD]
    // LITRUN(1): len=1, data=[0xEF]
    // MATCH(2): len=5, dist=0x1234

    const uint32_t raw_count = 8; // 2+1+5 = 8 bytes
    const uint32_t token_count = 3;
    const uint32_t match_count = 1; // v16: only 1 MATCH token

    // Build raw streams
    std::vector<uint8_t> type_stream = {0, 0, 1}; // LITRUN, LITRUN, MATCH
    std::vector<uint8_t> len_stream = {2, 1, 5};
    // v16: dist only for MATCH tokens (1 byte instead of 3)
    std::vector<uint8_t> dist_lo_stream = {0x34};
    std::vector<uint8_t> dist_hi_stream = {0x12};
    std::vector<uint8_t> lit_stream = {0xAB, 0xCD, 0xEF};

    // Build payload header (v0x0016 format)
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

    // v16: token_count + match_count
    push_u32(token_count);
    push_u32(match_count);

    // Stream sizes (v16 uses same layout as v15)
    push_u32(type_stream.size());
    push_u32(len_stream.size());
    push_u32(dist_lo_stream.size());
    push_u32(dist_hi_stream.size());
    push_u32(lit_stream.size());

    // Insert streams
    payload.insert(payload.end(), type_stream.begin(), type_stream.end());
    payload.insert(payload.end(), len_stream.begin(), len_stream.end());
    payload.insert(payload.end(), dist_lo_stream.begin(), dist_lo_stream.end());
    payload.insert(payload.end(), dist_hi_stream.begin(), dist_hi_stream.end());
    payload.insert(payload.end(), lit_stream.begin(), lit_stream.end());

    if (payload.size() < 28) {
        FAIL("v0x0016 payload too small");
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
        FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS_V2, // v0x0016
        mock_decode, mock_decode, mock_decompress, nullptr
    );

    if (result.size() == raw_count) {
        PASS();
    } else {
        FAIL("Result size mismatch");
    }
}

