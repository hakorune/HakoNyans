#pragma once

#include <string>

#include "../src/codec/encode.h"
#include "bench_bit_accounting_common.h"

namespace bench_bit_accounting {

void print_lossless_mode_stats(
    const hakonyans::GrayscaleEncoder::LosslessModeDebugStats& s
);

void print_lossless_json(
    const std::string& image_path,
    int width,
    int height,
    const Accounting& a,
    const hakonyans::GrayscaleEncoder::LosslessModeDebugStats& s
);

} // namespace bench_bit_accounting
