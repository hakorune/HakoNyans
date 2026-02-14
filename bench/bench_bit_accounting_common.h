#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bench_bit_accounting {

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

Accounting analyze_file(const std::vector<uint8_t>& hkn);
void print_accounting(const std::string& title, const Accounting& a, bool lossless);

} // namespace bench_bit_accounting
