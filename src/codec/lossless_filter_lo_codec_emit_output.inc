    std::vector<uint8_t> lo_stream;
    if (best_mode == 0) {
        lo_stream = std::move(lo_legacy);
        if (stats) stats->filter_lo_mode0++;
    } else if (best_mode == 3) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(3);

        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));

        uint32_t ps = (uint32_t)pred_stream.size();
        lo_stream.push_back((uint8_t)(ps & 0xFF));
        lo_stream.push_back((uint8_t)((ps >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((ps >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((ps >> 24) & 0xFF));

        lo_stream.insert(lo_stream.end(), pred_stream.begin(), pred_stream.end());
        lo_stream.insert(lo_stream.end(), resid_stream.begin(), resid_stream.end());

        if (stats) {
            stats->filter_lo_mode3++;
            stats->filter_lo_mode3_rows_sum += mode3_preds.size();
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode3_saved_bytes_sum += (lo_legacy.size() - lo_stream.size());
            }
            for (uint8_t p : mode3_preds) {
                if (p < 4) stats->filter_lo_mode3_pred_hist[p]++;
            }
        }
    } else if (best_mode == 4) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(4);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
        for (int k = 0; k < 6; k++) {
            uint32_t len = (uint32_t)mode4_streams[k].size();
            lo_stream.push_back((uint8_t)(len & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
        }
        for (int k = 0; k < 6; k++) {
            lo_stream.insert(lo_stream.end(), mode4_streams[k].begin(), mode4_streams[k].end());
        }
        if (stats) {
            stats->filter_lo_mode4++;
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode4_saved_bytes_sum +=
                    (uint64_t)(lo_legacy.size() - lo_stream.size());
            }
            int nonempty_ctx = 0;
            for (int k = 0; k < 6; k++) {
                stats->filter_lo_ctx_bytes_sum[k] += mode4_ctx_raw_counts[k];
                if (mode4_ctx_raw_counts[k] > 0) nonempty_ctx++;
            }
            if (nonempty_ctx > 0) {
                stats->filter_lo_ctx_nonempty_tiles++;
            }
        }
    } else if (best_mode == 7) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(7);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
        lo_stream.push_back((uint8_t)(mode7_shared_mask & 0xFF));
        lo_stream.push_back((uint8_t)((mode7_shared_mask >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((mode7_shared_mask >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((mode7_shared_mask >> 24) & 0xFF));
        for (int k = 0; k < 6; k++) {
            uint32_t len = (uint32_t)mode7_streams[k].size();
            lo_stream.push_back((uint8_t)(len & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
        }
        for (int k = 0; k < 6; k++) {
            lo_stream.insert(lo_stream.end(), mode7_streams[k].begin(), mode7_streams[k].end());
        }
        if (stats) {
            stats->filter_lo_mode7++;
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode7_saved_bytes_sum +=
                    (uint64_t)(lo_legacy.size() - lo_stream.size());
            }
            int nonempty_ctx = 0;
            for (int k = 0; k < 6; k++) {
                stats->filter_lo_ctx_bytes_sum[k] += mode7_ctx_raw_counts[k];
                if (mode7_ctx_raw_counts[k] > 0) nonempty_ctx++;
            }
            if (nonempty_ctx > 0) {
                stats->filter_lo_ctx_nonempty_tiles++;
            }
        }
    } else if (best_mode == 8) {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back(8);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));
        // Write ctx_codec_ids[6]
        for (int k = 0; k < 6; k++) {
            lo_stream.push_back(mode8_output_codec_ids[k]);
        }
        // Write lens[6]
        for (int k = 0; k < 6; k++) {
            uint32_t len = (uint32_t)mode8_output_streams[k].size();
            lo_stream.push_back((uint8_t)(len & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 8) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 16) & 0xFF));
            lo_stream.push_back((uint8_t)((len >> 24) & 0xFF));
        }
        // Write ctx streams
        for (int k = 0; k < 6; k++) {
            lo_stream.insert(lo_stream.end(), mode8_output_streams[k].begin(), mode8_output_streams[k].end());
        }
        if (stats) {
            stats->filter_lo_mode8++;
            if (lo_legacy.size() > lo_stream.size()) {
                stats->filter_lo_mode8_saved_bytes_sum +=
                    (uint64_t)(lo_legacy.size() - lo_stream.size());
            }
            int nonempty_ctx = 0;
            for (int k = 0; k < 6; k++) {
                stats->filter_lo_ctx_bytes_sum[k] += mode8_output_ctx_raw_counts[k];
                if (mode8_output_ctx_raw_counts[k] > 0) nonempty_ctx++;
            }
            if (nonempty_ctx > 0) {
                stats->filter_lo_ctx_nonempty_tiles++;
            }
        }
    } else {
        lo_stream.clear();
        lo_stream.push_back(FileHeader::WRAPPER_MAGIC_FILTER_LO);
        lo_stream.push_back((uint8_t)best_mode);
        uint32_t rc = (uint32_t)lo_bytes.size();
        lo_stream.push_back((uint8_t)(rc & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 8) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 16) & 0xFF));
        lo_stream.push_back((uint8_t)((rc >> 24) & 0xFF));

        if (best_mode == 1) {
            lo_stream.insert(lo_stream.end(), delta_rans.begin(), delta_rans.end());
            if (stats) stats->filter_lo_mode1++;
        } else if (best_mode == 2) {
            lo_stream.insert(lo_stream.end(), lo_lz.begin(), lo_lz.end());
            if (stats) stats->filter_lo_mode2++;
        } else if (best_mode == 6) {
            lo_stream = std::move(lo_mode6_encoded);
            if (stats) {
                stats->filter_lo_mode6++;
                if (lo_legacy.size() > lo_stream.size()) {
                    stats->filter_lo_mode6_saved_bytes_sum +=
                        (uint64_t)(lo_legacy.size() - lo_stream.size());
                }
            }
        } else {
            lo_stream.insert(lo_stream.end(), lo_lz_rans.begin(), lo_lz_rans.end());
            if (stats) {
                stats->filter_lo_mode5++;
                if (lo_legacy.size() > lo_stream.size()) {
                    stats->filter_lo_mode5_saved_bytes_sum +=
                        (uint64_t)(lo_legacy.size() - lo_stream.size());
                }
            }
        }
    }

    if (stats) stats->filter_lo_compressed_bytes_sum += lo_stream.size();
    return lo_stream;
