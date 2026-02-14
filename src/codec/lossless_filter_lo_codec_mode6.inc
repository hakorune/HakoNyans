    // Mode6: Token-RANS - parse LZ into tokens and entropy code each stream
    std::vector<uint8_t> lo_mode6_encoded;
    size_t mode6_wrapped = std::numeric_limits<size_t>::max();
    uint32_t mode6_token_count = 0;
    bool mode6_considered = false;
    bool mode6_parse_ok = false;
    const bool mode6_enable = get_mode6_enable();
    if (mode6_enable && evaluate_lz &&
        lo_bytes.size() >= (size_t)mode6_params.min_raw_bytes &&
        lo_lz.size() >= (size_t)mode6_params.min_lz_bytes) {
        mode6_considered = true;
        if (stats) stats->filter_lo_mode6_candidates++;
        const auto t_mode6_eval0 = Clock::now();

        // Mode6 v0x0017: type bitpack + len split
        std::vector<uint8_t> type_bits, lit_len, match_len, dist_lo_stream, dist_hi_stream, lit_stream;
        uint32_t mode6_lit_token_count = 0;
        uint32_t mode6_match_count = 0;
        mode6_parse_ok = parse_tilelz_to_tokens_v17(
            lo_lz, type_bits, lit_len, match_len, dist_lo_stream, dist_hi_stream, lit_stream,
            mode6_token_count, mode6_lit_token_count, mode6_match_count
        );

        if (mode6_parse_ok) {
            // Sanity check: token_count == lit_token_count + match_count
            if (mode6_token_count != mode6_lit_token_count + mode6_match_count) {
                mode6_parse_ok = false;
                if (stats) stats->filter_lo_mode6_malformed_input++;
            } else {
                auto type_bits_enc = encode_byte_stream_shared_lz(type_bits);
                auto lit_len_enc = encode_byte_stream_shared_lz(lit_len);
                auto match_len_enc = encode_byte_stream_shared_lz(match_len);
                auto dist_lo_enc = encode_byte_stream_shared_lz(dist_lo_stream);
                auto dist_hi_enc = encode_byte_stream_shared_lz(dist_hi_stream);
                auto lit_enc = encode_byte_stream_shared_lz(lit_stream);

                // Payload v0x0017: [magic=0xAB][mode=6][raw_count][token_count][match_count][lit_token_count]
                //                  [type_bits_sz][lit_len_sz][match_len_sz][dist_lo_sz][dist_hi_sz][lit_sz]
                //                  [type_bits_enc][lit_len_enc][match_len_enc][dist_lo_enc][dist_hi_enc][lit_enc]
                // Header: 2 + 4 + 4 + 4 + 4 + 4*6 = 2 + 16 + 24 = 42 bytes minimum
                size_t header_size = 2 + 4 + 4 + 4 + 4 + 4 * 6; // magic+mode + raw_count + token_count + match_count + lit_token_count + 6 stream sizes
                mode6_wrapped = header_size + type_bits_enc.size() + lit_len_enc.size() + match_len_enc.size()
                                + dist_lo_enc.size() + dist_hi_enc.size() + lit_enc.size();

                if (mode6_wrapped < best_size) {
                    lo_mode6_encoded.clear();
                    lo_mode6_encoded.reserve(mode6_wrapped);
                    lo_mode6_encoded.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
                    lo_mode6_encoded.push_back(6);
                    uint32_t rc = (uint32_t)lo_bytes.size();
                    lo_mode6_encoded.push_back((uint8_t)(rc & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((rc >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((rc >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((rc >> 24) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)(mode6_token_count & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_token_count >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_token_count >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_token_count >> 24) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)(mode6_match_count & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_match_count >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_match_count >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_match_count >> 24) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)(mode6_lit_token_count & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_lit_token_count >> 8) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_lit_token_count >> 16) & 0xFF));
                    lo_mode6_encoded.push_back((uint8_t)((mode6_lit_token_count >> 24) & 0xFF));
                    auto push_size = [&](size_t sz) {
                        lo_mode6_encoded.push_back((uint8_t)(sz & 0xFF));
                        lo_mode6_encoded.push_back((uint8_t)((sz >> 8) & 0xFF));
                        lo_mode6_encoded.push_back((uint8_t)((sz >> 16) & 0xFF));
                        lo_mode6_encoded.push_back((uint8_t)((sz >> 24) & 0xFF));
                    };
                    push_size(type_bits_enc.size());
                    push_size(lit_len_enc.size());
                    push_size(match_len_enc.size());
                    push_size(dist_lo_enc.size());
                    push_size(dist_hi_enc.size());
                    push_size(lit_enc.size());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), type_bits_enc.begin(), type_bits_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), lit_len_enc.begin(), lit_len_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), match_len_enc.begin(), match_len_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), dist_lo_enc.begin(), dist_lo_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), dist_hi_enc.begin(), dist_hi_enc.end());
                    lo_mode6_encoded.insert(lo_mode6_encoded.end(), lit_enc.begin(), lit_enc.end());
                }

                if (stats) {
                    stats->filter_lo_mode6_match_tokens_sum += mode6_match_count;
                    stats->filter_lo_mode6_lit_tokens_sum += mode6_lit_token_count;
                    stats->filter_lo_mode6_token_count_sum += mode6_token_count;
                    stats->filter_lo_mode6_match_count_sum += mode6_match_count;
                    // Calculate dist bytes saved by compact format vs legacy
                    uint64_t dist_saved = (uint64_t)mode6_lit_token_count * 2; // 2 bytes per LIT token
                    stats->filter_lo_mode6_dist_saved_bytes_sum += dist_saved;
                    // v0x0017 specific counters
                    stats->filter_lo_mode6_typebits_raw_bytes_sum += type_bits.size();
                    stats->filter_lo_mode6_typebits_enc_bytes_sum += type_bits_enc.size();
                    stats->filter_lo_mode6_lit_len_bytes_sum += lit_len_enc.size();
                    stats->filter_lo_mode6_match_len_bytes_sum += match_len_enc.size();
                }
            }
        } else {
            if (stats) stats->filter_lo_mode6_malformed_input++;
        }

        if (stats) {
            stats->filter_lo_mode6_eval_ns += ns_since(t_mode6_eval0, Clock::now());
            stats->filter_lo_mode6_candidate_bytes_sum += lo_lz.size();
            if (mode6_wrapped != std::numeric_limits<size_t>::max()) {
                stats->filter_lo_mode6_wrapped_bytes_sum += mode6_wrapped;
            }
            stats->filter_lo_mode6_legacy_bytes_sum += legacy_size;
        }
    }

    if (mode6_wrapped != std::numeric_limits<size_t>::max()) {
        bool better_than_legacy = (mode6_wrapped * 1000 <= legacy_size * (size_t)mode6_params.gain_permille);
        bool better_than_lz = (mode6_wrapped * 1000 <= lz_wrapped * (size_t)mode6_params.vs_lz_permille);

        if (better_than_legacy && better_than_lz) {
            if (mode6_wrapped < best_size) {
                best_size = mode6_wrapped;
                best_mode = 6;
                if (stats) stats->filter_lo_mode6_v17_selected++;
            } else {
                if (stats) stats->filter_lo_mode6_reject_best++;
            }
        } else {
            if (stats) stats->filter_lo_mode6_reject_gate++;
        }
    }

    if (stats && mode6_considered && best_mode != 6) {
        if (best_mode == 5) stats->filter_lo_mode6_fallback_to_mode5++;
        else if (best_mode == 0) stats->filter_lo_mode6_fallback_to_mode0++;
    }
