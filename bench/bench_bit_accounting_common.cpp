#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../src/codec/headers.h"
#include "bench_bit_accounting_common.h"

namespace bench_bit_accounting {
using namespace hakonyans;

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

Accounting analyze_file(const std::vector<uint8_t>& hkn) {
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

void print_accounting(const std::string& title, const Accounting& a, bool lossless) {
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

} // namespace bench_bit_accounting
