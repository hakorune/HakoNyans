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
    size_t pindex = 0;
    size_t qdelta = 0;
    size_t cfl = 0;
    size_t filter_ids = 0;
    size_t filter_lo = 0;
    size_t filter_hi = 0;
    size_t block_types = 0;
    size_t palette = 0;
    size_t copy = 0;
    size_t unknown = 0;
};

static void add_lossy_tile(Accounting& a, const uint8_t* tile_data, size_t tile_size) {
    if (tile_size < 32) {
        a.unknown += tile_size;
        return;
    }
    uint32_t sz[8] = {};
    std::memcpy(sz, tile_data, 32);
    a.tile_header += 32;
    a.dc += sz[0];
    a.ac += sz[1];
    a.pindex += sz[2];
    a.qdelta += sz[3];
    a.cfl += sz[4];
    a.block_types += sz[5];
    a.palette += sz[6];
    a.copy += sz[7];
    size_t used = 32ull + sz[0] + sz[1] + sz[2] + sz[3] + sz[4] + sz[5] + sz[6] + sz[7];
    if (tile_size > used) a.unknown += (tile_size - used);
}

static void add_lossless_tile(Accounting& a, const uint8_t* tile_data, size_t tile_size) {
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
    size_t used = 32ull + sz[0] + sz[1] + sz[2] + sz[4] + sz[5] + sz[6];
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
            else add_lossy_tile(a, ptr, (size_t)e.size);
        } else {
            a.unknown += e.size;
        }
    }

    size_t accounted =
        a.file_header + a.chunk_dir + a.qmat + a.tile_header +
        a.dc + a.ac + a.pindex + a.qdelta + a.cfl +
        a.filter_ids + a.filter_lo + a.filter_hi +
        a.block_types + a.palette + a.copy + a.unknown;
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
        print_row("ac_stream", a.ac, a.total_file);
        print_row("pindex", a.pindex, a.total_file);
        print_row("qdelta", a.qdelta, a.total_file);
        print_row("cfl", a.cfl, a.total_file);
    }
    print_row("block_types", a.block_types, a.total_file);
    print_row("palette", a.palette, a.total_file);
    print_row("copy", a.copy, a.total_file);
    print_row("unknown", a.unknown, a.total_file);
    std::cout << "----------------------------------------------\n";
    print_row("TOTAL", a.total_file, a.total_file);
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
        auto a = analyze_file(hkn);
        print_accounting("Lossless", a, true);
    }

    if (do_lossy) {
        auto hkn = GrayscaleEncoder::encode_color(ppm.rgb_data.data(), ppm.width, ppm.height, (uint8_t)quality, true, true, false);
        auto a = analyze_file(hkn);
        print_accounting("Lossy (Q=" + std::to_string(quality) + ")", a, false);
    }

    return 0;
}
