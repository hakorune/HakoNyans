#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#include "../src/codec/encode.h"
#include "../src/codec/headers.h"
#include "ppm_loader.h"

using namespace hakonyans;

struct Accounting {
    size_t total_file = 0;
    size_t file_header = 0;
    size_t chunk_dir = 0;
    size_t qmat = 0;
    size_t tile_header = 0;
    size_t dc = 0;
    size_t ac = 0;
    size_t ac_low = 0;
    size_t ac_mid = 0;
    size_t ac_high = 0;
    size_t pindex = 0;
    size_t pindex_checkpoints = 0;
    size_t qdelta = 0;
    size_t cfl = 0;
    size_t filter_ids = 0;
    size_t filter_lo = 0;
    size_t filter_hi = 0;
    size_t block_types = 0;
    size_t palette = 0;
    size_t copy = 0;
    size_t tile4 = 0;
    size_t screen_index = 0;
    size_t natural_row = 0;
    size_t unknown = 0;
};

static size_t estimate_pindex_cps(size_t bytes) {
    if (bytes < 12) return 0;
    if (((bytes - 12) % 40) != 0) return 0;
    return (bytes - 12) / 40;
}

static void add_lossy_tile(Accounting& a, const uint8_t* tile_data, size_t tile_size, bool has_band_cdf) {
    if (!has_band_cdf && tile_size < 32) {
        a.unknown += tile_size;
        return;
    }
    if (has_band_cdf && tile_size < 40) {
        a.unknown += tile_size;
        return;
    }
    if (has_band_cdf) {
        uint32_t sz[10] = {};
        std::memcpy(sz, tile_data, 40);
        a.tile_header += 40;
        a.dc += sz[0];
        a.ac_low += sz[1];
        a.ac_mid += sz[2];
        a.ac_high += sz[3];
        a.ac += (size_t)sz[1] + (size_t)sz[2] + (size_t)sz[3];
        a.pindex += sz[4];
        if (sz[4] >= 12) {
            // v3 band pindex blob:
            // [low_sz u32][mid_sz u32][high_sz u32][low][mid][high]
            size_t pi_off = 40ull + (size_t)sz[0] + (size_t)sz[1] + (size_t)sz[2] + (size_t)sz[3];
            if (pi_off + 12 <= tile_size) {
                uint32_t band_pi_sz[3] = {};
                std::memcpy(band_pi_sz, tile_data + pi_off, 12);
                size_t expected = 12ull + (size_t)band_pi_sz[0] + (size_t)band_pi_sz[1] + (size_t)band_pi_sz[2];
                if (expected == sz[4]) {
                    a.pindex_checkpoints += estimate_pindex_cps(band_pi_sz[0]);
                    a.pindex_checkpoints += estimate_pindex_cps(band_pi_sz[1]);
                    a.pindex_checkpoints += estimate_pindex_cps(band_pi_sz[2]);
                } else {
                    a.pindex_checkpoints += estimate_pindex_cps(sz[4]);
                }
            } else {
                a.pindex_checkpoints += estimate_pindex_cps(sz[4]);
            }
        }
        a.qdelta += sz[5];
        a.cfl += sz[6];
        a.block_types += sz[7];
        a.palette += sz[8];
        a.copy += sz[9];
        size_t used = 40ull + sz[0] + sz[1] + sz[2] + sz[3] + sz[4] + sz[5] + sz[6] + sz[7] + sz[8] + sz[9];
        if (tile_size > used) a.unknown += (tile_size - used);
    } else {
        uint32_t sz[8] = {};
        std::memcpy(sz, tile_data, 32);
        a.tile_header += 32;
        a.dc += sz[0];
        a.ac += sz[1];
        a.pindex += sz[2];
        a.pindex_checkpoints += estimate_pindex_cps(sz[2]);
        a.qdelta += sz[3];
        a.cfl += sz[4];
        a.block_types += sz[5];
        a.palette += sz[6];
        a.copy += sz[7];
        size_t used = 32ull + sz[0] + sz[1] + sz[2] + sz[3] + sz[4] + sz[5] + sz[6] + sz[7];
        if (tile_size > used) a.unknown += (tile_size - used);
    }
}

static void add_lossless_tile(Accounting& a, const uint8_t* tile_data, size_t tile_size) {
    if (tile_size >= 18 && tile_data[0] == FileHeader::WRAPPER_MAGIC_NATURAL_ROW) {
        auto read_u32 = [](const uint8_t* p) -> uint32_t {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        };
        uint8_t mode = tile_data[1];
        uint32_t pred_count = read_u32(tile_data + 6);
        uint32_t resid_payload_size = read_u32(tile_data + 14);
        size_t header_bytes = 0;
        size_t payload_off = 0;
        uint32_t pred_payload_size = 0;
        if (mode == 0) {
            header_bytes = 18;
            payload_off = header_bytes + (size_t)pred_count;
        } else if (mode == 1 || mode == 2) {
            if (tile_size < 27) {
                a.unknown += tile_size;
                return;
            }
            pred_payload_size = read_u32(tile_data + 23);
            header_bytes = 27;
            payload_off = header_bytes + (size_t)pred_payload_size;
        } else if (mode == 3) {
            if (tile_size < 27) {
                a.unknown += tile_size;
                return;
            }
            uint32_t flat_size = read_u32(tile_data + 10);
            uint32_t edge_size = read_u32(tile_data + 14);
            pred_payload_size = read_u32(tile_data + 23);
            header_bytes = 27;
            payload_off = header_bytes + (size_t)pred_payload_size;
            resid_payload_size = flat_size + edge_size;
        } else {
            a.unknown += tile_size;
            return;
        }

        if (payload_off > tile_size) {
            a.unknown += tile_size;
            return;
        }

        a.tile_header += header_bytes;
        a.filter_ids += pred_count; // row predictor ids
        size_t payload_bytes = tile_size - payload_off;
        if (payload_bytes > 0) a.natural_row += payload_bytes;
        uint64_t expected_payload = (uint64_t)resid_payload_size;
        if (mode == 1 || mode == 2 || mode == 3) expected_payload += (uint64_t)pred_payload_size;
        if ((uint64_t)payload_bytes > expected_payload) {
            a.unknown += (size_t)((uint64_t)payload_bytes - expected_payload);
        }
        return;
    }

    if (tile_size >= 14 && tile_data[0] == FileHeader::WRAPPER_MAGIC_SCREEN_INDEXED) {
        auto read_u16 = [](const uint8_t* p) -> uint16_t {
            return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        };
        auto read_u32 = [](const uint8_t* p) -> uint32_t {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        };
        uint16_t palette_count = read_u16(tile_data + 4);
        uint32_t raw_packed_size = read_u32(tile_data + 10);
        size_t palette_bytes = (size_t)palette_count * 2ull;
        size_t header_bytes = 14;
        if (header_bytes + palette_bytes > tile_size) {
            a.unknown += tile_size;
            return;
        }
        a.tile_header += header_bytes;
        a.palette += palette_bytes;
        a.screen_index += (tile_size - header_bytes - palette_bytes);
        if (raw_packed_size == 0 && tile_size > header_bytes + palette_bytes) {
            a.unknown += 0;
        }
        return;
    }

    if (tile_size < 32) {
        a.unknown += tile_size;
        return;
    }
    uint32_t sz[8] = {};
    std::memcpy(sz, tile_data, 32);
    a.tile_header += 32;
    a.filter_ids += sz[0];
    a.filter_lo += sz[1];
    a.filter_hi += sz[2];
    a.block_types += sz[4];
    a.palette += sz[5];
    a.copy += sz[6];
    a.tile4 += sz[7];
    size_t used = 32ull + sz[0] + sz[1] + sz[2] + sz[4] + sz[5] + sz[6] + sz[7];
    if (tile_size > used) a.unknown += (tile_size - used);
}

static Accounting analyze_file(const std::vector<uint8_t>& hkn) {
    Accounting a;
    a.total_file = hkn.size();
    if (hkn.size() < 48) {
        a.unknown = hkn.size();
        return a;
    }

    a.file_header = 48;
    FileHeader hdr = FileHeader::read(hkn.data());
    ChunkDirectory dir = ChunkDirectory::deserialize(&hkn[48], hkn.size() - 48);
    a.chunk_dir = dir.serialized_size();

    for (const auto& e : dir.entries) {
        if (e.offset + e.size > hkn.size()) {
            a.unknown += e.size;
            continue;
        }
        const uint8_t* ptr = &hkn[e.offset];
        std::string type = e.type_str();
        if (type == "QMAT") {
            a.qmat += e.size;
        } else if (type.rfind("TIL", 0) == 0 || type == "TILE") {
            if (hdr.flags & 1) add_lossless_tile(a, ptr, (size_t)e.size);
            else add_lossy_tile(a, ptr, (size_t)e.size, hdr.has_band_group_cdf());
        } else {
            a.unknown += e.size;
        }
    }

    size_t accounted =
        a.file_header + a.chunk_dir + a.qmat + a.tile_header +
        a.dc + a.ac + a.pindex + a.qdelta + a.cfl +
        a.filter_ids + a.filter_lo + a.filter_hi +
        a.block_types + a.palette + a.copy + a.tile4 +
        a.screen_index + a.natural_row + a.unknown;
    if (a.total_file > accounted) a.unknown += (a.total_file - accounted);
    return a;
}

static void print_row(const std::string& key, size_t bytes, size_t total) {
    double pct = (total > 0) ? (100.0 * (double)bytes / (double)total) : 0.0;
    std::cout << std::left << std::setw(18) << key
              << std::right << std::setw(12) << bytes
              << std::setw(10) << std::fixed << std::setprecision(2) << pct << "%\n";
}

static void print_accounting(const std::string& title, const Accounting& a, bool lossless) {
    std::cout << "\n=== " << title << " ===\n";
    std::cout << std::left << std::setw(18) << "Component"
              << std::right << std::setw(12) << "Bytes"
              << std::setw(10) << "Share\n";
    std::cout << "----------------------------------------------\n";
    print_row("file_header", a.file_header, a.total_file);
    print_row("chunk_dir", a.chunk_dir, a.total_file);
    print_row("qmat", a.qmat, a.total_file);
    print_row("tile_header", a.tile_header, a.total_file);
    if (lossless) {
        print_row("filter_ids", a.filter_ids, a.total_file);
        print_row("filter_lo", a.filter_lo, a.total_file);
        print_row("filter_hi", a.filter_hi, a.total_file);
    } else {
        print_row("dc_stream", a.dc, a.total_file);
        if (a.ac_low > 0 || a.ac_mid > 0 || a.ac_high > 0) {
            print_row("ac_low", a.ac_low, a.total_file);
            print_row("ac_mid", a.ac_mid, a.total_file);
            print_row("ac_high", a.ac_high, a.total_file);
        } else {
            print_row("ac_stream", a.ac, a.total_file);
        }
        print_row("PINDEX", a.pindex, a.total_file);
        if (a.pindex > 0) {
            std::cout << std::left << std::setw(18) << "pindex_cps"
                      << std::right << std::setw(12) << a.pindex_checkpoints
                      << std::setw(10) << "-" << "\n";
        }
        print_row("qdelta", a.qdelta, a.total_file);
        print_row("cfl", a.cfl, a.total_file);
    }
    print_row("block_types", a.block_types, a.total_file);
    print_row("palette", a.palette, a.total_file);
    print_row("copy", a.copy, a.total_file);
    print_row("tile4", a.tile4, a.total_file);
    print_row("screen_index", a.screen_index, a.total_file);
    print_row("natural_row", a.natural_row, a.total_file);
    print_row("unknown", a.unknown, a.total_file);
    std::cout << "----------------------------------------------\n";
    print_row("TOTAL", a.total_file, a.total_file);
}

static void print_mode_stat_row(const std::string& key, uint64_t value, uint64_t total) {
    double pct = (total > 0) ? (100.0 * (double)value / (double)total) : 0.0;
    std::cout << "  " << std::left << std::setw(20) << key
              << std::right << std::setw(10) << value
              << std::setw(10) << std::fixed << std::setprecision(2) << pct << "%\n";
}

static void print_lossless_mode_stats(const GrayscaleEncoder::LosslessModeDebugStats& s) {
    std::cout << "\nMode Selection Stats (Lossless)\n";
    std::cout << "----------------------------------------------\n";
    std::cout << "  total_blocks           " << s.total_blocks << "\n";
    print_mode_stat_row("tile4_candidates", s.tile4_candidates, s.total_blocks);
    print_mode_stat_row("copy_candidates", s.copy_candidates, s.total_blocks);
    print_mode_stat_row("palette_candidates", s.palette_candidates, s.total_blocks);
    print_mode_stat_row("copy_palette_overlap", s.copy_palette_overlap, s.total_blocks);
    print_mode_stat_row("tile4_selected", s.tile4_selected, s.total_blocks);
    print_mode_stat_row("copy_selected", s.copy_selected, s.total_blocks);
    print_mode_stat_row("palette_selected", s.palette_selected, s.total_blocks);
    print_mode_stat_row("filter_any_selected", s.filter_selected, s.total_blocks);
    // filter_med_selected is count of ROWS using MED. 
    // total_rows = total_blocks * 8 (approx, for Full HD 1080p height is padded)
    // For now just show the raw count.
    std::cout << "  filter_med_rows        " << std::right << std::setw(10) << s.filter_med_selected << "\n";

    if (s.total_blocks > 0) {
        double tile4_cand_bpb = (s.tile4_candidates > 0)
            ? (double)s.est_tile4_bits_sum / (double)s.tile4_candidates : 0.0;
        double copy_cand_bpb = (s.copy_candidates > 0)
            ? (double)s.est_copy_bits_sum / (double)s.copy_candidates : 0.0;
        double palette_cand_bpb = (s.palette_candidates > 0)
            ? (double)s.est_palette_bits_sum / (double)s.palette_candidates : 0.0;
        double selected_bpb = (double)s.est_selected_bits_sum / (double)s.total_blocks;
        double filter_bpb = (double)s.est_filter_bits_sum / (double)s.total_blocks;
        double gain_pct = (s.est_filter_bits_sum > 0)
            ? (100.0 * ((double)s.est_filter_bits_sum - (double)s.est_selected_bits_sum) / (double)s.est_filter_bits_sum)
            : 0.0;

        std::cout << "  est_tile4_bits_sum     " << s.est_tile4_bits_sum
                  << " (avg " << std::fixed << std::setprecision(2) << tile4_cand_bpb << " bits/candidate)\n";
        std::cout << "  est_copy_bits_sum      " << s.est_copy_bits_sum
                  << " (avg " << std::fixed << std::setprecision(2) << copy_cand_bpb << " bits/candidate)\n";
        std::cout << "  est_palette_bits_sum   " << s.est_palette_bits_sum
                  << " (avg " << std::fixed << std::setprecision(2) << palette_cand_bpb << " bits/candidate)\n";
        std::cout << "  est_selected_bits      " << s.est_selected_bits_sum << "\n";
        std::cout << "  est_filter_bits        " << s.est_filter_bits_sum << "\n";
        std::cout << "  est_bits_per_block     "
                  << std::fixed << std::setprecision(2)
                  << selected_bpb << " (selected) / " << filter_bpb << " (filter-only)\n";
        std::cout << "  est_gain_vs_filter     "
                  << std::fixed << std::setprecision(2) << gain_pct << "%\n";

        uint64_t tile4_rejected = s.tile4_rejected_by_copy + s.tile4_rejected_by_palette + s.tile4_rejected_by_filter;
        uint64_t copy_rejected = s.copy_rejected_by_tile4 + s.copy_rejected_by_palette + s.copy_rejected_by_filter;
        uint64_t palette_rejected = s.palette_rejected_by_tile4 + s.palette_rejected_by_copy + s.palette_rejected_by_filter;

        auto print_win_rate = [](const std::string& key, uint64_t wins, uint64_t cands) {
            double pct = (cands > 0) ? (100.0 * (double)wins / (double)cands) : 0.0;
            std::cout << "  " << std::left << std::setw(25) << key
                      << std::right << std::setw(8) << std::fixed << std::setprecision(2) << pct << "%\n";
        };
        std::cout << "\n  Candidate win-rate\n";
        print_win_rate("tile4_win_rate", s.tile4_selected, s.tile4_candidates);
        print_win_rate("copy_win_rate", s.copy_selected, s.copy_candidates);
        print_win_rate("palette_win_rate", s.palette_selected, s.palette_candidates);
        if (s.palette_rescue_attempted > 0) {
            double rescue_pct = 100.0 * (double)s.palette_rescue_adopted /
                                (double)s.palette_rescue_attempted;
            std::cout << "  palette_rescue_adopted " << s.palette_rescue_adopted
                      << "/" << s.palette_rescue_attempted
                      << " (" << std::fixed << std::setprecision(2) << rescue_pct << "%)\n";
            if (s.palette_rescue_adopted > 0) {
                double avg_gain = (double)s.palette_rescue_gain_bits_sum /
                                  (double)s.palette_rescue_adopted;
                std::cout << "  palette_rescue_avg_gain "
                          << std::fixed << std::setprecision(2)
                          << avg_gain << " bits/block\n";
            }
        }

        auto print_reject_row = [](const std::string& name, uint64_t val, uint64_t total) {
            double pct = (total > 0) ? (100.0 * (double)val / (double)total) : 0.0;
            std::cout << "    " << std::left << std::setw(22) << name
                      << std::right << std::setw(8) << val
                      << std::setw(9) << std::fixed << std::setprecision(2) << pct << "%\n";
        };

        std::cout << "\n  Rejected reasons (tile4 candidates)\n";
        print_reject_row("lost_to_copy", s.tile4_rejected_by_copy, tile4_rejected);
        print_reject_row("lost_to_palette", s.tile4_rejected_by_palette, tile4_rejected);
        print_reject_row("lost_to_filter", s.tile4_rejected_by_filter, tile4_rejected);

        std::cout << "  Rejected reasons (copy candidates)\n";
        print_reject_row("lost_to_tile4", s.copy_rejected_by_tile4, copy_rejected);
        print_reject_row("lost_to_palette", s.copy_rejected_by_palette, copy_rejected);
        print_reject_row("lost_to_filter", s.copy_rejected_by_filter, copy_rejected);

        std::cout << "  Rejected reasons (palette candidates)\n";
        print_reject_row("lost_to_tile4", s.palette_rejected_by_tile4, palette_rejected);
        print_reject_row("lost_to_copy", s.palette_rejected_by_copy, palette_rejected);
        print_reject_row("lost_to_filter", s.palette_rejected_by_filter, palette_rejected);

        auto print_loss_bits = [](const std::string& key, uint64_t loss_bits, uint64_t rejected) {
            double avg = (rejected > 0) ? (double)loss_bits / (double)rejected : 0.0;
            std::cout << "  " << std::left << std::setw(25) << key
                      << std::right << std::setw(10) << loss_bits
                      << " (avg " << std::fixed << std::setprecision(2) << avg << " bits/reject)\n";
        };
        std::cout << "\n  Estimated loss vs chosen mode\n";
        print_loss_bits("tile4_loss_bits_sum", s.est_tile4_loss_bits_sum, tile4_rejected);
        print_loss_bits("copy_loss_bits_sum", s.est_copy_loss_bits_sum, copy_rejected);
        print_loss_bits("palette_loss_bits_sum", s.est_palette_loss_bits_sum, palette_rejected);

        if (s.filter_selected > 0) {
            std::cout << "\n  Filter block diagnostics\n";
            std::cout << "  filter_selected_blocks  " << s.filter_selected << "\n";
            std::cout << "  filter_with_copy/pal    "
                      << s.filter_blocks_with_copy_candidate << "/"
                      << s.filter_blocks_with_palette_candidate << "\n";
            std::cout << "  filter_unique<=2/4/8/>8 "
                      << s.filter_blocks_unique_le2 << "/"
                      << s.filter_blocks_unique_le4 << "/"
                      << s.filter_blocks_unique_le8 << "/"
                      << s.filter_blocks_unique_gt8 << "\n";
            double avg_trans = (double)s.filter_blocks_transitions_sum / (double)s.filter_selected;
            double avg_var_proxy = (double)s.filter_blocks_variance_proxy_sum / (double)s.filter_selected;
            double avg_filter_bits = (double)s.filter_blocks_est_filter_bits_sum / (double)s.filter_selected;
            std::cout << "  filter_avg_transitions  " << std::fixed << std::setprecision(2) << avg_trans << "\n";
            std::cout << "  filter_avg_var_proxy    " << std::fixed << std::setprecision(2) << avg_var_proxy << "\n";
            std::cout << "  filter_avg_est_bits     " << std::fixed << std::setprecision(2) << avg_filter_bits << " bits/block\n";

            if (s.filter_diag_palette16_candidates > 0) {
                double better_pct = 100.0 * (double)s.filter_diag_palette16_better /
                                    (double)s.filter_diag_palette16_candidates;
                double avg_pal_size = (double)s.filter_diag_palette16_size_sum /
                                      (double)s.filter_diag_palette16_candidates;
                double avg_pal_bits = (double)s.filter_diag_palette16_est_bits_sum /
                                      (double)s.filter_diag_palette16_candidates;
                std::cout << "  palette8_candidates     " << s.filter_diag_palette16_candidates
                          << " (avg size " << std::fixed << std::setprecision(2) << avg_pal_size
                          << ", avg " << avg_pal_bits << " bits/block)\n";
                std::cout << "  palette8_better_than_filter " << s.filter_diag_palette16_better
                          << " (" << std::fixed << std::setprecision(2) << better_pct << "%)\n";
                if (s.filter_diag_palette16_better > 0) {
                    double avg_gain = (double)s.filter_diag_palette16_gain_bits_sum /
                                      (double)s.filter_diag_palette16_better;
                    std::cout << "  palette8_avg_gain_bits  " << std::fixed << std::setprecision(2)
                              << avg_gain << " bits/block\n";
                }
            }
            if (s.filter_rows_with_pixels > 0) {
                std::cout << "  filter_rows_with_pixels " << s.filter_rows_with_pixels << "\n";
                std::cout << "  filter_row_hist(N/S/U/A/P/M) "
                          << s.filter_row_id_hist[0] << "/"
                          << s.filter_row_id_hist[1] << "/"
                          << s.filter_row_id_hist[2] << "/"
                          << s.filter_row_id_hist[3] << "/"
                          << s.filter_row_id_hist[4] << "/"
                          << s.filter_row_id_hist[5] << "\n";
            }
        }

        double bt_bpb = (s.total_blocks > 0)
            ? (8.0 * (double)s.block_types_bytes_sum / (double)s.total_blocks) : 0.0;
        double pal_bpb = (s.palette_selected > 0)
            ? (8.0 * (double)s.palette_stream_bytes_sum / (double)s.palette_selected) : 0.0;
        double tile4_bpb = (s.tile4_selected > 0)
            ? (8.0 * (double)s.tile4_stream_bytes_sum / (double)s.tile4_selected) : 0.0;
        std::cout << "\n  Encoded stream cost\n";
        std::cout << "  block_types_bytes      " << s.block_types_bytes_sum
                  << " (avg " << std::fixed << std::setprecision(2) << bt_bpb << " bits/block)\n";
        if (s.block_types_lz_used_count > 0) {
            std::cout << "    lz_tiles/saved       " 
                      << s.block_types_lz_used_count << " / " << s.block_types_lz_saved_bytes_sum << " bytes\n";
        }
        if (s.block_type_runs_sum > 0) {
            double avg_run = (double)s.total_blocks / (double)s.block_type_runs_sum;
            std::cout << "  block_type_runs        " << s.block_type_runs_sum
                      << " (avg run " << std::fixed << std::setprecision(2) << avg_run << ")\n";
            std::cout << "    runs_short(<=2)      " << s.block_type_short_runs << "\n";
            std::cout << "    runs_long(>=16)      " << s.block_type_long_runs << "\n";
            std::cout << "    runs_dct/pal/cpy/t4  "
                      << s.block_type_runs_dct << "/"
                      << s.block_type_runs_palette << "/"
                      << s.block_type_runs_copy << "/"
                      << s.block_type_runs_tile4 << "\n";
        }
        std::cout << "  palette_stream_bytes   " << s.palette_stream_bytes_sum
                  << " (avg " << std::fixed << std::setprecision(2) << pal_bpb << " bits/palette-block)\n";
        if (s.palette_lz_used_count > 0) {
             std::cout << "    lz_tiles/saved       " 
                       << s.palette_lz_used_count << " / " << s.palette_lz_saved_bytes_sum << " bytes\n";
        }
        if (s.palette_stream_raw_bytes_sum > 0) {
            double raw_bpb = (s.palette_selected > 0)
                ? (8.0 * (double)s.palette_stream_raw_bytes_sum / (double)s.palette_selected) : 0.0;
            std::cout << "  palette_raw_bytes      " << s.palette_stream_raw_bytes_sum
                      << " (avg " << std::fixed << std::setprecision(2) << raw_bpb << " bits/palette-block)\n";
        }
        std::cout << "  tile4_stream_bytes     " << s.tile4_stream_bytes_sum
                  << " (avg " << std::fixed << std::setprecision(2) << tile4_bpb << " bits/tile4-block)\n";
        if (s.tile4_stream_raw_bytes_sum > 0) {
            double savings = 100.0 * (1.0 - (double)s.tile4_stream_bytes_sum / (double)s.tile4_stream_raw_bytes_sum);
            std::cout << "  tile4_raw_bytes        " << s.tile4_stream_raw_bytes_sum
                      << " (wrapper " << std::fixed << std::setprecision(1) << savings << "% savings)\n";
        }
        if (s.tile4_stream_mode0 + s.tile4_stream_mode1 + s.tile4_stream_mode2 > 0) {
            std::cout << "  tile4_stream_mode0/1/2 " << s.tile4_stream_mode0
                      << "/" << s.tile4_stream_mode1
                      << "/" << s.tile4_stream_mode2 << "\n";
        }

        std::cout << "\n  Palette stream diagnostics\n";
        std::cout << "  palette_stream_v2/v3   "
                  << s.palette_stream_v2_count << "/"
                  << s.palette_stream_v3_count << "\n";
        std::cout << "  mask_dict_streams      " << s.palette_stream_mask_dict_count
                  << " (entries " << s.palette_stream_mask_dict_entries << ")\n";
        std::cout << "  palette_dict_streams   " << s.palette_stream_palette_dict_count
                  << " (entries " << s.palette_stream_palette_dict_entries << ")\n";
        if (s.palette_blocks_parsed > 0) {
            double prev_pct = 100.0 * (double)s.palette_blocks_prev_reuse / (double)s.palette_blocks_parsed;
            double dict_ref_pct = 100.0 * (double)s.palette_blocks_dict_ref / (double)s.palette_blocks_parsed;
            double raw_pct = 100.0 * (double)s.palette_blocks_raw_colors / (double)s.palette_blocks_parsed;
            std::cout << "  palette_blocks_parsed  " << s.palette_blocks_parsed << "\n";
            std::cout << "    prev/dict/raw        "
                      << s.palette_blocks_prev_reuse << "/"
                      << s.palette_blocks_dict_ref << "/"
                      << s.palette_blocks_raw_colors
                      << " (" << std::fixed << std::setprecision(2)
                      << prev_pct << "% / " << dict_ref_pct << "% / " << raw_pct << "%)\n";
            std::cout << "    size<=2 / size>2     "
                      << s.palette_blocks_two_color << " / " << s.palette_blocks_multi_color << "\n";
        }
        if (s.palette_stream_compact_count > 0) {
            std::cout << "  compact_palette_used   " << s.palette_stream_compact_count
                      << " (saved " << s.palette_stream_compact_saved_bytes_sum << " bytes)\n";
        }
        if (s.palette_parse_errors > 0) {
            std::cout << "  palette_parse_errors   " << s.palette_parse_errors << "\n";
        }
        if (s.palette_reorder_trials > 0) {
             std::cout << "  reorder_trials         " << s.palette_reorder_trials << "\n";
             std::cout << "  reorder_adopted        " << s.palette_reorder_adopted
                       << " (" << std::fixed << std::setprecision(1) 
                       << (100.0 * s.palette_reorder_adopted / s.palette_reorder_trials) << "%)\n";
        }

        std::cout << "\n  Copy stream diagnostics\n";
        std::cout << "  copy_stream_count      " << s.copy_stream_count << "\n";
        std::cout << "  copy_stream_mode0/1/2/3  "
                  << s.copy_stream_mode0 << "/"
                  << s.copy_stream_mode1 << "/"
                  << s.copy_stream_mode2 << "/"
                  << s.copy_stream_mode3 << "\n";
        std::cout << "  copy_wrapper_mode0/1/2 " 
                  << s.copy_wrap_mode0 << "/"
                  << s.copy_wrap_mode1 << "/"
                  << s.copy_wrap_mode2 << "\n";
        std::cout << "  copy_ops_total         " << s.copy_ops_total << "\n";
        if (s.copy_ops_total > 0) {
            double small_pct = 100.0 * (double)s.copy_ops_small / (double)s.copy_ops_total;
            std::cout << "    small/raw ops        " << s.copy_ops_small << "/" << s.copy_ops_raw
                      << " (" << std::fixed << std::setprecision(2) << small_pct << "% small)\n";
        }
        if (s.copy_stream_count > 0) {
            double avg_bytes_stream = (double)s.copy_stream_bytes_sum / (double)s.copy_stream_count;
            std::cout << "  copy_stream_bytes      " << s.copy_stream_bytes_sum
                      << " (avg " << std::fixed << std::setprecision(2) << avg_bytes_stream << " B/stream)\n";
            std::cout << "    lz_used/saved        " << s.copy_lz_used_count << " / " << s.copy_lz_saved_bytes_sum << " bytes\n";
        }
        if (s.copy_ops_total > 0) {
            double bits_per_copy = 8.0 * (double)s.copy_stream_bytes_sum / (double)s.copy_ops_total;
            std::cout << "  copy_effective_bits    "
                      << std::fixed << std::setprecision(2) << bits_per_copy << " bits/copy-op\n";
        }
        std::cout << "  copy_payload_bits      " << s.copy_stream_payload_bits_sum << "\n";
        std::cout << "  copy_overhead_bits     " << s.copy_stream_overhead_bits_sum << "\n";
        if (s.copy_stream_mode2 > 0) {
            double avg_dyn_bits = (double)s.copy_mode2_dynamic_bits_sum / (double)s.copy_stream_mode2;
            std::cout << "  mode2_zero_bit_streams " << s.copy_mode2_zero_bit_streams << "\n";
            std::cout << "  mode2_avg_dyn_bits     " << std::fixed << std::setprecision(2) << avg_dyn_bits << "\n";
        }
        if (s.copy_stream_mode3 > 0) {
            double avg_run_len = (s.copy_mode3_run_tokens_sum > 0) ?
                (double)s.copy_mode3_runs_sum / (double)s.copy_mode3_run_tokens_sum : 0.0;
            std::cout << "  mode3_run_tokens       " << s.copy_mode3_run_tokens_sum << "\n";
            std::cout << "  mode3_avg_run_length   " << std::fixed << std::setprecision(2) << avg_run_len << "\n";
            std::cout << "  mode3_long_runs(>=16)  " << s.copy_mode3_long_runs << "\n";
        }
    }

    // Filter stream diagnostics (Phase 9n)
    {
        uint64_t fid_total = s.filter_ids_mode0 + s.filter_ids_mode1 + s.filter_ids_mode2;
        if (fid_total > 0 || s.filter_hi_sparse_count + s.filter_hi_dense_count > 0) {
            std::cout << "\n  Filter stream diagnostics\n";
            std::cout << "  filter_ids_mode0/1/2   "
                      << s.filter_ids_mode0 << "/"
                      << s.filter_ids_mode1 << "/"
                      << s.filter_ids_mode2 << "\n";
            if (s.filter_ids_raw_bytes_sum > 0) {
                double savings = 100.0 * (1.0 - (double)s.filter_ids_compressed_bytes_sum / (double)s.filter_ids_raw_bytes_sum);
                std::cout << "  filter_ids_bytes       raw=" << s.filter_ids_raw_bytes_sum
                          << " compressed=" << s.filter_ids_compressed_bytes_sum
                          << " (" << std::fixed << std::setprecision(1) << savings << "% savings)\n";
            }
            uint64_t fhi_total = s.filter_hi_sparse_count + s.filter_hi_dense_count;
            std::cout << "  filter_hi_sparse/dense " << s.filter_hi_sparse_count << "/" << s.filter_hi_dense_count << "\n";
            if (fhi_total > 0) {
                std::cout << "  filter_hi_avg_zero_ratio " << std::fixed << std::setprecision(1)
                          << (double)s.filter_hi_zero_ratio_sum / (double)fhi_total << "%\n";
            }
            if (s.filter_hi_raw_bytes_sum > 0) {
                double savings = 100.0 * (1.0 - (double)s.filter_hi_compressed_bytes_sum / (double)s.filter_hi_raw_bytes_sum);
                std::cout << "  filter_hi_bytes        raw=" << s.filter_hi_raw_bytes_sum
                          << " compressed=" << s.filter_hi_compressed_bytes_sum
                          << " (" << std::fixed << std::setprecision(1) << savings << "% savings)\n";
            }
        }
    }

    // Filter lo diagnostics (Phase 9o / 9p / 9q / 9u-next / 9x)
    {
        uint64_t flo_total = s.filter_lo_mode0 + s.filter_lo_mode1 + s.filter_lo_mode2 +
                             s.filter_lo_mode3 + s.filter_lo_mode4 + s.filter_lo_mode5 + s.filter_lo_mode6;
        if (flo_total > 0) {
            std::cout << "\n  Filter lo diagnostics\n";
            std::cout << "  filter_lo_mode0/1/2/3/4/5/6  "
                      << s.filter_lo_mode0 << "/"
                      << s.filter_lo_mode1 << "/"
                      << s.filter_lo_mode2 << "/"
                      << s.filter_lo_mode3 << "/"
                      << s.filter_lo_mode4 << "/"
                      << s.filter_lo_mode5 << "/"
                      << s.filter_lo_mode6 << "\n";
            if (s.filter_lo_raw_bytes_sum > 0) {
                double savings = 100.0 * (1.0 - (double)s.filter_lo_compressed_bytes_sum / (double)s.filter_lo_raw_bytes_sum);
                std::cout << "  filter_lo_bytes        raw=" << s.filter_lo_raw_bytes_sum
                          << " compressed=" << s.filter_lo_compressed_bytes_sum
                          << " (" << std::fixed << std::setprecision(1) << savings << "% savings)\n";
            }
            if (s.filter_lo_mode3 > 0) {
                std::cout << "  mode3_rows_total       " << s.filter_lo_mode3_rows_sum << "\n";
                if (s.filter_lo_mode3_saved_bytes_sum > 0)
                     std::cout << "  mode3_saved_bytes      " << s.filter_lo_mode3_saved_bytes_sum << "\n";
                
                uint64_t ph_total = s.filter_lo_mode3_pred_hist[0] + s.filter_lo_mode3_pred_hist[1] + 
                                    s.filter_lo_mode3_pred_hist[2] + s.filter_lo_mode3_pred_hist[3];
                if (ph_total > 0) {
                    std::cout << "  mode3_pred_hist        NONE:" 
                              << (100.0 * s.filter_lo_mode3_pred_hist[0] / ph_total) << "% SUB:"
                              << (100.0 * s.filter_lo_mode3_pred_hist[1] / ph_total) << "% UP:"
                              << (100.0 * s.filter_lo_mode3_pred_hist[2] / ph_total) << "% AVG:"
                              << (100.0 * s.filter_lo_mode3_pred_hist[3] / ph_total) << "%\n";
                }
            }
            if (s.filter_lo_mode4 > 0) {
                if (s.filter_lo_mode4_saved_bytes_sum > 0) {
                    std::cout << "  mode4_saved_bytes      " << s.filter_lo_mode4_saved_bytes_sum << "\n";
                }
                std::cout << "  mode4_nonempty_tiles   " << s.filter_lo_ctx_nonempty_tiles << "\n";
                std::cout << "  mode4_ctx_raw_bytes    "
                          << s.filter_lo_ctx_bytes_sum[0] << "/"
                          << s.filter_lo_ctx_bytes_sum[1] << "/"
                          << s.filter_lo_ctx_bytes_sum[2] << "/"
                          << s.filter_lo_ctx_bytes_sum[3] << "/"
                          << s.filter_lo_ctx_bytes_sum[4] << "/"
                          << s.filter_lo_ctx_bytes_sum[5] << "\n";
            }
            if (s.filter_lo_mode5 > 0 && s.filter_lo_mode5_saved_bytes_sum > 0) {
                std::cout << "  mode5_saved_bytes      " << s.filter_lo_mode5_saved_bytes_sum << "\n";
            }
            if (s.filter_lo_mode5_candidates > 0) {
                std::cout << "  mode5_candidates       " << s.filter_lo_mode5_candidates << "\n";
                std::cout << "    rej_gate/rej_best    " << s.filter_lo_mode5_reject_gate << "/" << s.filter_lo_mode5_reject_best << "\n";
                double avg_cand = (double)s.filter_lo_mode5_candidate_bytes_sum / (double)s.filter_lo_mode5_candidates;
                double avg_wrap = (double)s.filter_lo_mode5_wrapped_bytes_sum / (double)s.filter_lo_mode5_candidates;
                double avg_leg  = (double)s.filter_lo_mode5_legacy_bytes_sum / (double)s.filter_lo_mode5_candidates;
                std::cout << "    avg_bytes(LZ/Wrap/Leg) " << std::fixed << std::setprecision(1)
                          << avg_cand << " / " << avg_wrap << " / " << avg_leg << "\n";
            }
            if (s.filter_lo_mode6 > 0 && s.filter_lo_mode6_saved_bytes_sum > 0) {
                std::cout << "  mode6_saved_bytes      " << s.filter_lo_mode6_saved_bytes_sum << "\n";
            }
            if (s.filter_lo_mode6_candidates > 0) {
                std::cout << "  mode6_candidates       " << s.filter_lo_mode6_candidates << "\n";
                std::cout << "    rej_gate/rej_best    " << s.filter_lo_mode6_reject_gate << "/" << s.filter_lo_mode6_reject_best << "\n";
                double avg_cand = (double)s.filter_lo_mode6_candidate_bytes_sum / (double)s.filter_lo_mode6_candidates;
                double avg_wrap = (double)s.filter_lo_mode6_wrapped_bytes_sum / (double)s.filter_lo_mode6_candidates;
                double avg_leg  = (double)s.filter_lo_mode6_legacy_bytes_sum / (double)s.filter_lo_mode6_candidates;
                std::cout << "    avg_bytes(LZ/Wrap/Leg) " << std::fixed << std::setprecision(1)
                          << avg_cand << " / " << avg_wrap << " / " << avg_leg << "\n";
                // Mode6 compact dist telemetry (Phase 9X)
                if (s.filter_lo_mode6_token_count_sum > 0) {
                    std::cout << "    tokens(match+lit)    " << s.filter_lo_mode6_token_count_sum
                              << " (" << s.filter_lo_mode6_match_tokens_sum << "+" << s.filter_lo_mode6_lit_tokens_sum << ")\n";
                    double avg_match = (double)s.filter_lo_mode6_match_count_sum / (double)s.filter_lo_mode6_candidates;
                    std::cout << "    avg_match_tokens       " << std::fixed << std::setprecision(1) << avg_match << "\n";
                    if (s.filter_lo_mode6_dist_saved_bytes_sum > 0) {
                        std::cout << "    dist_saved_bytes     " << s.filter_lo_mode6_dist_saved_bytes_sum << "\n";
                    }
                }
                // Mode6 v0x0017 telemetry (Phase 9X-2)
                if (s.filter_lo_mode6_v17_selected > 0) {
                    std::cout << "    v17_selected         " << s.filter_lo_mode6_v17_selected << "\n";
                    if (s.filter_lo_mode6_typebits_raw_bytes_sum > 0) {
                        double typebits_ratio = (double)s.filter_lo_mode6_typebits_enc_bytes_sum /
                                                (double)s.filter_lo_mode6_typebits_raw_bytes_sum;
                        std::cout << "    typebits raw/enc     " << s.filter_lo_mode6_typebits_raw_bytes_sum
                                  << "/" << s.filter_lo_mode6_typebits_enc_bytes_sum
                                  << " (ratio=" << std::fixed << std::setprecision(3) << typebits_ratio << ")\n";
                    }
                    if (s.filter_lo_mode6_lit_len_bytes_sum > 0 || s.filter_lo_mode6_match_len_bytes_sum > 0) {
                        std::cout << "    lit_len/match_len    " << s.filter_lo_mode6_lit_len_bytes_sum
                                  << "/" << s.filter_lo_mode6_match_len_bytes_sum << "\n";
                    }
                }
                if (s.filter_lo_mode6_fallback_to_mode0 > 0 || s.filter_lo_mode6_fallback_to_mode5 > 0) {
                    std::cout << "    fallback_to_mode0/5  " << s.filter_lo_mode6_fallback_to_mode0
                              << "/" << s.filter_lo_mode6_fallback_to_mode5 << "\n";
                }
                if (s.filter_lo_mode6_malformed_input > 0) {
                    std::cout << "    malformed_input      " << s.filter_lo_mode6_malformed_input << "\n";
                }
            }
            if (s.filter_lo_mode2_reject_gate > 0 || s.filter_lo_mode4_reject_gate > 0) {
                std::cout << "  rej_gate(mode2/4)      " << s.filter_lo_mode2_reject_gate << "/" << s.filter_lo_mode4_reject_gate << "\n";
            }
            if (s.filter_lo_lz_probe_enabled > 0) {
                std::cout << "  lz_probe enabled/chk/pass/skip "
                          << s.filter_lo_lz_probe_enabled << "/"
                          << s.filter_lo_lz_probe_checked << "/"
                          << s.filter_lo_lz_probe_pass << "/"
                          << s.filter_lo_lz_probe_skip << "\n";
                if (s.filter_lo_lz_probe_sample_bytes_sum > 0) {
                    double ratio = (double)s.filter_lo_lz_probe_sample_wrapped_bytes_sum /
                                   (double)s.filter_lo_lz_probe_sample_bytes_sum;
                    std::cout << "    probe_sample raw/lz/wrapped "
                              << s.filter_lo_lz_probe_sample_bytes_sum << "/"
                              << s.filter_lo_lz_probe_sample_lz_bytes_sum << "/"
                              << s.filter_lo_lz_probe_sample_wrapped_bytes_sum
                              << " (wrapped/raw=" << std::fixed << std::setprecision(3)
                              << ratio << ")\n";
                }
            }
        }

    }

    // Phase 9s-3: Screen-indexed gating telemetry
    {
        uint64_t sc_total = s.screen_candidate_count;
        if (sc_total > 0) {
            std::cout << "\n  Screen-indexed gating diagnostics\n";
            std::cout << "  screen_candidates      " << sc_total << "\n";
            std::cout << "  screen_selected        " << s.screen_selected_count << "\n";
            
            auto print_reject = [](const std::string& key, uint64_t val, uint64_t total) {
                double pct = (total > 0) ? (100.0 * (double)val / (double)total) : 0.0;
                std::cout << "    " << std::left << std::setw(21) << key
                          << std::right << std::setw(8) << val
                          << std::setw(9) << std::fixed << std::setprecision(2) << pct << "%\n";
            };
            uint64_t rejected_total = s.screen_rejected_pre_gate + s.screen_rejected_cost_gate;
            std::cout << "  screen_rejected        " << rejected_total << "\n";
            print_reject("pre_gate", s.screen_rejected_pre_gate, rejected_total);
            print_reject("cost_gate", s.screen_rejected_cost_gate, rejected_total);
            if (s.screen_rejected_pre_gate > 0) {
                std::cout << "    pre_small_tile       " << s.screen_rejected_small_tile << "\n";
                std::cout << "    pre_texture_gate     " << s.screen_rejected_prefilter_texture << "\n";
                std::cout << "    pre_build_fail       " << s.screen_rejected_build_fail << "\n";
                if (s.screen_rejected_build_fail > 0) {
                    std::cout << "      fail_too_many_unique " << s.screen_build_fail_too_many_unique << "\n";
                    std::cout << "      fail_empty_hist      " << s.screen_build_fail_empty_hist << "\n";
                    std::cout << "      fail_index_miss      " << s.screen_build_fail_index_miss << "\n";
                    std::cout << "      fail_other           " << s.screen_build_fail_other << "\n";
                }
                std::cout << "    pre_palette_limit    " << s.screen_rejected_palette_limit << "\n";
                std::cout << "    pre_bits_limit       " << s.screen_rejected_bits_limit << "\n";
            }
            
            if (s.screen_mode0_reject_count > 0) {
                std::cout << "    (mode0_too_big)      " << s.screen_mode0_reject_count << "\n";
            }
            
            uint64_t gate_total = s.screen_ui_like_count + s.screen_anime_like_count;
            if (gate_total > 0) {
                 std::cout << "  screen_profile_types   UI:" << s.screen_ui_like_count 
                           << " Anime:" << s.screen_anime_like_count << "\n";
            }

            std::cout << "\n";
            auto print_gain = [](const std::string& key, uint64_t val) {
                 std::cout << "  " << std::left << std::setw(23) << key << val << "\n";
            };

            if (s.screen_selected_count > 0) {
                 double avg_pal = (double)s.screen_palette_count_sum / (double)sc_total;
                 std::cout << "  avg_palette_count      " << std::fixed << std::setprecision(1) << avg_pal << "\n";
                 print_gain("total_gain_bytes", s.screen_gain_bytes_sum);
            }
            if (s.screen_compete_legacy_bytes_sum > 0) {
                 double ratio = (double)s.screen_compete_screen_bytes_sum /
                                (double)s.screen_compete_legacy_bytes_sum;
                 std::cout << "  compete_ratio(screen/legacy) " << std::fixed << std::setprecision(4)
                           << ratio << "\n";
            }
            if (s.screen_prefilter_eval_count > 0) {
                double avg_unique = (double)s.screen_prefilter_unique_sum /
                                    (double)s.screen_prefilter_eval_count;
                double avg_run = ((double)s.screen_prefilter_avg_run_x100_sum /
                                  (double)s.screen_prefilter_eval_count) / 100.0;
                double avg_mad = ((double)s.natural_prefilter_mad_x100_sum /
                                  (double)s.screen_prefilter_eval_count) / 100.0;
                double avg_entropy = ((double)s.natural_prefilter_entropy_x100_sum /
                                      (double)s.screen_prefilter_eval_count) / 100.0;
                std::cout << "  prefilter_avg(unique/run/mad/entropy) "
                          << std::fixed << std::setprecision(1)
                          << avg_unique << " / " << avg_run << " / "
                          << avg_mad << " / " << avg_entropy << "\n";
            }
            if (s.screen_rejected_cost_gate > 0) {
                 print_gain("total_avoided_bloat", s.screen_loss_bytes_sum);
            }
        }

        if (s.natural_row_candidate_count > 0) {
            std::cout << "\n  Natural-row route diagnostics\n";
            std::cout << "  natural_candidates     " << s.natural_row_candidate_count << "\n";
            std::cout << "  natural_selected       " << s.natural_row_selected_count << "\n";
            std::cout << "  natural_rej_cost       " << s.natural_row_rejected_cost_gate << "\n";
            std::cout << "  natural_build_fail     " << s.natural_row_build_fail_count << "\n";
            if (s.natural_row_gain_bytes_sum > 0) {
                std::cout << "  natural_gain_bytes     " << s.natural_row_gain_bytes_sum << "\n";
            }
            if (s.natural_row_loss_bytes_sum > 0) {
                std::cout << "  natural_loss_bytes     " << s.natural_row_loss_bytes_sum << "\n";
            }
            std::cout << "  natural_row_mode0/1/2/3 " 
                      << s.natural_row_mode0_selected_count << "/"
                      << s.natural_row_mode1_selected_count << "/"
                      << s.natural_row_mode2_selected_count << "/"
                      << s.natural_row_mode3_selected_count << "\n";
            if (s.natural_prefilter_eval_count > 0) {
                double avg_unique = (double)s.natural_prefilter_unique_sum /
                                    (double)s.natural_prefilter_eval_count;
                double avg_run = ((double)s.natural_prefilter_avg_run_x100_sum /
                                  (double)s.natural_prefilter_eval_count) / 100.0;
                double avg_mad = ((double)s.natural_prefilter_mad_x100_sum /
                                  (double)s.natural_prefilter_eval_count) / 100.0;
                double avg_entropy = ((double)s.natural_prefilter_entropy_x100_sum /
                                      (double)s.natural_prefilter_eval_count) / 100.0;
                std::cout << "  natural_prefilter      pass/rej="
                          << s.natural_prefilter_pass_count << "/"
                          << s.natural_prefilter_reject_count << "\n";
                std::cout << "  natural_prefilter_avg  unique/run/mad/entropy="
                          << std::fixed << std::setprecision(1)
                          << avg_unique << " / " << avg_run << " / "
                          << avg_mad << " / " << avg_entropy << "\n";
            }
        }
        if (s.route_compete_policy_skip_count > 0) {
            std::cout << "\n  Route policy diagnostics\n";
            std::cout << "  route_policy_skipped   " << s.route_compete_policy_skip_count << "\n";
        }
        {
            const uint64_t total_parallel_paths =
                s.perf_encode_plane_parallel_3way_count +
                s.perf_encode_plane_parallel_2way_count +
                s.perf_encode_plane_parallel_seq_count;
            if (total_parallel_paths > 0) {
                std::cout << "\n  Encode parallel diagnostics\n";
                std::cout << "  plane_parallel_3way    " << s.perf_encode_plane_parallel_3way_count << "\n";
                std::cout << "  plane_parallel_2way    " << s.perf_encode_plane_parallel_2way_count << "\n";
                std::cout << "  plane_parallel_seq     " << s.perf_encode_plane_parallel_seq_count << "\n";
                std::cout << "  plane_parallel_tokens  " << s.perf_encode_plane_parallel_tokens_sum << "\n";
            }
        }

        std::cout << "\n  Lossless profile diagnostics\n";
        std::cout << "  profile_ui_tiles       " << s.profile_ui_tiles << "\n";
        std::cout << "  profile_anime_tiles    " << s.profile_anime_tiles << "\n";
        std::cout << "  profile_photo_tiles    " << s.profile_photo_tiles << "\n";

        uint64_t class_evals = s.class_eval_count;
        if (class_evals == 0) {
            class_evals = s.profile_ui_tiles + s.profile_anime_tiles + s.profile_photo_tiles;
        }
        if (class_evals > 0) {
            std::cout << "\n  Classifier diagnostics\n";
            std::cout << "    classifier_evals       " << class_evals << "\n";
            std::cout << "    avg_copy_hit_rate      " << std::fixed << std::setprecision(3)
                      << (double)s.class_copy_hit_x1000_sum / (double)(class_evals * 1000.0) << "\n";
            std::cout << "    avg_mean_abs_diff      " << std::fixed << std::setprecision(1)
                      << (double)s.class_mean_abs_diff_x1000_sum / (double)(class_evals * 1000.0) << "\n";
            std::cout << "    avg_active_bins        " << std::fixed << std::setprecision(1)
                      << (double)s.class_active_bins_sum / (double)class_evals << "\n";
            std::cout << "    anime_palette_bonus    " << s.anime_palette_bonus_applied << "\n";
        }

        if (s.palette_reorder_trials > 0) {
            std::cout << "\n  Palette Reorder diagnostics\n";
            std::cout << "  reorder_trials         " << s.palette_reorder_trials << "\n";
            std::cout << "  reorder_adopted        " << s.palette_reorder_adopted << "\n";
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image.ppm> [--quality Q] [--lossless] [--lossy]\n";
        return 1;
    }

    std::string path = argv[1];
    int quality = 75;
    bool do_lossless = true;
    bool do_lossy = true;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--quality" && i + 1 < argc) {
            quality = std::clamp(std::stoi(argv[++i]), 1, 100);
        } else if (arg == "--lossless") {
            do_lossy = false;
            do_lossless = true;
        } else if (arg == "--lossy") {
            do_lossless = false;
            do_lossy = true;
        }
    }

    auto ppm = load_ppm(path);
    std::cout << "Image: " << path << " (" << ppm.width << "x" << ppm.height << ")\n";

    if (do_lossless) {
        auto hkn = GrayscaleEncoder::encode_color_lossless(ppm.rgb_data.data(), ppm.width, ppm.height);
        auto mode_stats = GrayscaleEncoder::get_lossless_mode_debug_stats();
        auto a = analyze_file(hkn);
        print_accounting("Lossless", a, true);
        print_lossless_mode_stats(mode_stats);
    }

    if (do_lossy) {
        auto hkn = GrayscaleEncoder::encode_color(ppm.rgb_data.data(), ppm.width, ppm.height, (uint8_t)quality, true, true, false);
        auto a = analyze_file(hkn);
        print_accounting("Lossy (Q=" + std::to_string(quality) + ")", a, false);
    }

    return 0;
}
