            // Mode 6: v0x0017 (type bitpack + len split), v0x0016 (compact dist), or v0x0015 (legacy)
            // v0x0017: type_bits + lit_len + match_len + dist + lit, payload min = 36 bytes
            // v0x0016: type[] + len[] + dist[] (MATCH only) + lit, payload min = 28 bytes
            // v0x0015: type[] + len[] + dist[] (all tokens) + lit, payload min = 24 bytes
            const bool is_v17 = (file_version >= FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS_V3);
            const bool is_v16 = (!is_v17 && file_version >= FileHeader::VERSION_FILTER_LO_LZ_TOKEN_RANS_V2);
            const size_t min_payload_size = is_v17 ? 36 : (is_v16 ? 28 : 24);

            if (payload_size < min_payload_size) {
                lo_bytes.assign(raw_count, 0);
                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
            } else {
                if (stats) stats->plane_filter_lo_mode6_count++;

                uint32_t token_count = (uint32_t)payload[0]
                                     | ((uint32_t)payload[1] << 8)
                                     | ((uint32_t)payload[2] << 16)
                                     | ((uint32_t)payload[3] << 24);
                uint32_t match_count = 0;
                uint32_t lit_token_count = 0;
                bool is_v17_valid = false;

                size_t header_offset = 4; // After token_count
                if (is_v17) {
                    match_count = (uint32_t)payload[4]
                                | ((uint32_t)payload[5] << 8)
                                | ((uint32_t)payload[6] << 16)
                                | ((uint32_t)payload[7] << 24);
                    lit_token_count = (uint32_t)payload[8]
                                    | ((uint32_t)payload[9] << 8)
                                    | ((uint32_t)payload[10] << 16)
                                    | ((uint32_t)payload[11] << 24);
                    header_offset = 12; // After token_count + match_count + lit_token_count

                    // Pre-check: token_count == lit_token_count + match_count
                    if (token_count != lit_token_count + match_count) {
                        lo_bytes.assign(raw_count, 0);
                        if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                    } else {
                        is_v17_valid = true;
                    }
                } else if (is_v16) {
                    match_count = (uint32_t)payload[4]
                                | ((uint32_t)payload[5] << 8)
                                | ((uint32_t)payload[6] << 16)
                                | ((uint32_t)payload[7] << 24);
                    header_offset = 8; // After token_count + match_count
                }

                // Parse stream sizes based on version
                uint32_t type_sz, lit_len_sz = 0, match_len_sz = 0, dist_lo_sz, dist_hi_sz, lit_sz;

                if (is_v17) {
                    type_sz = (uint32_t)payload[header_offset]
                            | ((uint32_t)payload[header_offset + 1] << 8)
                            | ((uint32_t)payload[header_offset + 2] << 16)
                            | ((uint32_t)payload[header_offset + 3] << 24);
                    lit_len_sz = (uint32_t)payload[header_offset + 4]
                               | ((uint32_t)payload[header_offset + 5] << 8)
                               | ((uint32_t)payload[header_offset + 6] << 16)
                               | ((uint32_t)payload[header_offset + 7] << 24);
                    match_len_sz = (uint32_t)payload[header_offset + 8]
                                 | ((uint32_t)payload[header_offset + 9] << 8)
                                 | ((uint32_t)payload[header_offset + 10] << 16)
                                 | ((uint32_t)payload[header_offset + 11] << 24);
                    dist_lo_sz = (uint32_t)payload[header_offset + 12]
                               | ((uint32_t)payload[header_offset + 13] << 8)
                               | ((uint32_t)payload[header_offset + 14] << 16)
                               | ((uint32_t)payload[header_offset + 15] << 24);
                    dist_hi_sz = (uint32_t)payload[header_offset + 16]
                               | ((uint32_t)payload[header_offset + 17] << 8)
                               | ((uint32_t)payload[header_offset + 18] << 16)
                               | ((uint32_t)payload[header_offset + 19] << 24);
                    lit_sz = (uint32_t)payload[header_offset + 20]
                           | ((uint32_t)payload[header_offset + 21] << 8)
                           | ((uint32_t)payload[header_offset + 22] << 16)
                           | ((uint32_t)payload[header_offset + 23] << 24);
                } else {
                    // v0x0015 and v0x0016 share same stream layout
                    type_sz = (uint32_t)payload[header_offset]
                            | ((uint32_t)payload[header_offset + 1] << 8)
                            | ((uint32_t)payload[header_offset + 2] << 16)
                            | ((uint32_t)payload[header_offset + 3] << 24);
                    uint32_t len_sz = (uint32_t)payload[header_offset + 4]
                                    | ((uint32_t)payload[header_offset + 5] << 8)
                                    | ((uint32_t)payload[header_offset + 6] << 16)
                                    | ((uint32_t)payload[header_offset + 7] << 24);
                    lit_len_sz = len_sz; // For v15/v16, use common len stream
                    dist_lo_sz = (uint32_t)payload[header_offset + 8]
                               | ((uint32_t)payload[header_offset + 9] << 8)
                               | ((uint32_t)payload[header_offset + 10] << 16)
                               | ((uint32_t)payload[header_offset + 11] << 24);
                    dist_hi_sz = (uint32_t)payload[header_offset + 12]
                               | ((uint32_t)payload[header_offset + 13] << 8)
                               | ((uint32_t)payload[header_offset + 14] << 16)
                               | ((uint32_t)payload[header_offset + 15] << 24);
                    lit_sz = (uint32_t)payload[header_offset + 16]
                           | ((uint32_t)payload[header_offset + 17] << 8)
                           | ((uint32_t)payload[header_offset + 18] << 16)
                           | ((uint32_t)payload[header_offset + 19] << 24);
                }

                const size_t total_header = is_v17 ? (header_offset + 24) : (header_offset + 20);
                bool sizes_ok = (payload_size >= total_header);
                size_t remain = sizes_ok ? (payload_size - total_header) : 0;
                auto consume = [&](uint32_t sz) {
                    if (!sizes_ok) return;
                    if ((size_t)sz > remain) {
                        sizes_ok = false;
                        return;
                    }
                    remain -= (size_t)sz;
                };
                consume(type_sz);
                if (is_v17) {
                    consume(lit_len_sz);
                    consume(match_len_sz);
                } else {
                    consume(lit_len_sz); // len_sz for v15/v16
                }
                consume(dist_lo_sz);
                consume(dist_hi_sz);
                consume(lit_sz);
                if (sizes_ok && remain != 0) sizes_ok = false;

                if (is_v17 && !is_v17_valid) {
                    // v17 pre-check failed, already zero-filled above
                } else if (!sizes_ok) {
                    lo_bytes.assign(raw_count, 0);
                    if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                } else {
                    const uint8_t* type_ptr = payload + total_header;
                    const uint8_t* lit_len_ptr = type_ptr + type_sz;
                    const uint8_t* match_len_ptr = is_v17 ? (lit_len_ptr + lit_len_sz) : nullptr;
                    const uint8_t* dist_lo_ptr = is_v17 ? (match_len_ptr + match_len_sz) : (lit_len_ptr + lit_len_sz);
                    const uint8_t* dist_hi_ptr = dist_lo_ptr + dist_lo_sz;
                    const uint8_t* lit_ptr = dist_hi_ptr + dist_hi_sz;

                    // Decode streams
                    std::vector<uint8_t> type_stream, lit_len_stream, match_len_stream, dist_lo_stream, dist_hi_stream, lit_stream;
                    size_t expected_type_size = is_v17 ? ((token_count + 7) / 8) : token_count;
                    size_t expected_dist_size = is_v17 ? match_count : (is_v16 ? match_count : token_count);

                    if (use_shared_lz_cdf) {
                        if (stats) stats->plane_filter_lo_mode6_shared_cdf_count++;
                        type_stream = timed_decode_shared_rans(type_ptr, type_sz, expected_type_size);
                        if (is_v17) {
                            lit_len_stream = timed_decode_shared_rans(lit_len_ptr, lit_len_sz, lit_token_count);
                            match_len_stream = timed_decode_shared_rans(match_len_ptr, match_len_sz, match_count);
                        } else {
                            lit_len_stream = timed_decode_shared_rans(lit_len_ptr, lit_len_sz, token_count);
                        }
                        dist_lo_stream = timed_decode_shared_rans(dist_lo_ptr, dist_lo_sz, expected_dist_size);
                        dist_hi_stream = timed_decode_shared_rans(dist_hi_ptr, dist_hi_sz, expected_dist_size);
                        lit_stream = timed_decode_shared_rans(lit_ptr, lit_sz, 0);
                    } else {
                        if (stats) stats->plane_filter_lo_mode6_legacy_cdf_count++;
                        type_stream = timed_decode_rans(type_ptr, type_sz, expected_type_size);
                        if (is_v17) {
                            lit_len_stream = timed_decode_rans(lit_len_ptr, lit_len_sz, lit_token_count);
                            match_len_stream = timed_decode_rans(match_len_ptr, match_len_sz, match_count);
                        } else {
                            lit_len_stream = timed_decode_rans(lit_len_ptr, lit_len_sz, token_count);
                        }
                        dist_lo_stream = timed_decode_rans(dist_lo_ptr, dist_lo_sz, expected_dist_size);
                        dist_hi_stream = timed_decode_rans(dist_hi_ptr, dist_hi_sz, expected_dist_size);
                        lit_stream = timed_decode_rans(lit_ptr, lit_sz, 0);
                    }

                    // Strict stream length verification
                    bool stream_lengths_ok;
                    if (is_v17) {
                        stream_lengths_ok =
                            (type_stream.size() == expected_type_size) &&
                            (lit_len_stream.size() == lit_token_count) &&
                            (match_len_stream.size() == match_count) &&
                            (dist_lo_stream.size() == match_count) &&
                            (dist_hi_stream.size() == match_count);
                    } else {
                        stream_lengths_ok =
                            (type_stream.size() == token_count) &&
                            (lit_len_stream.size() == token_count) &&
                            (dist_lo_stream.size() == expected_dist_size) &&
                            (dist_hi_stream.size() == expected_dist_size);
                    }

                    if (!stream_lengths_ok) {
                        lo_bytes.assign(raw_count, 0);
                        if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                    } else {
                        // Reconstruct TileLZ byte stream from tokens
                        std::vector<uint8_t> lz_payload;
                        lz_payload.reserve(token_count * 4 + lit_stream.size());
                        size_t lit_pos = 0;
                        size_t dist_pos = 0;
                        size_t lit_len_pos = 0;
                        size_t match_len_pos = 0;
                        bool reconstruct_ok = true;

                        for (size_t i = 0; i < token_count && reconstruct_ok; i++) {
                            uint8_t type;
                            uint8_t len;

                            if (is_v17) {
                                // Extract type bit from packed bytes
                                size_t byte_idx = i / 8;
                                size_t bit_pos = i % 8;
                                type = (type_stream[byte_idx] >> bit_pos) & 1;

                                if (type == 0) { // LITRUN
                                    if (lit_len_pos >= lit_len_stream.size()) {
                                        reconstruct_ok = false;
                                        break;
                                    }
                                    len = lit_len_stream[lit_len_pos++];
                                } else { // MATCH
                                    if (match_len_pos >= match_len_stream.size()) {
                                        reconstruct_ok = false;
                                        break;
                                    }
                                    len = match_len_stream[match_len_pos++];
                                }
                            } else {
                                // v0x0015 and v0x0016 use separate type/len arrays
                                type = type_stream[i];
                                len = lit_len_stream[i];
                            }

                            if (type == 0) { // LITRUN
                                lz_payload.push_back(0);
                                lz_payload.push_back(len);
                                if (lit_pos + len > lit_stream.size()) {
                                    reconstruct_ok = false;
                                    break;
                                }
                                lz_payload.insert(lz_payload.end(), lit_stream.data() + lit_pos, lit_stream.data() + lit_pos + len);
                                lit_pos += len;
                            } else if (type == 1) { // MATCH
                                if (dist_pos >= dist_lo_stream.size() || dist_pos >= dist_hi_stream.size()) {
                                    reconstruct_ok = false;
                                    break;
                                }
                                uint8_t dlo = dist_lo_stream[dist_pos];
                                uint8_t dhi = dist_hi_stream[dist_pos];
                                dist_pos++;
                                lz_payload.push_back(1);
                                lz_payload.push_back(len);
                                lz_payload.push_back(dlo);
                                lz_payload.push_back(dhi);
                            } else {
                                reconstruct_ok = false;
                            }
                        }

                        // Final verification for v17
                        if (is_v17) {
                            reconstruct_ok = reconstruct_ok &&
                                (lit_pos == lit_stream.size()) &&
                                (dist_pos == match_count) &&
                                (lit_len_pos == lit_token_count) &&
                                (match_len_pos == match_count);
                        } else {
                            reconstruct_ok = reconstruct_ok &&
                                (lit_pos == lit_stream.size()) &&
                                (dist_pos == (is_v16 ? match_count : token_count));
                        }

                        // Final verification: all streams must be fully consumed
                        if (reconstruct_ok &&
                            lit_pos == lit_stream.size() &&
                            dist_pos == expected_dist_size &&
                            !lz_payload.empty()) {
                            lo_bytes = timed_decompress_lz(lz_payload.data(), lz_payload.size(), raw_count);
                            if (lo_bytes.size() != raw_count) {
                                lo_bytes.assign(raw_count, 0);
                                if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                            }
                        } else {
                            lo_bytes.assign(raw_count, 0);
                            if (stats) stats->plane_filter_lo_fallback_zero_fill_count++;
                        }
                    }
                }
            }
