#pragma once

#include "bench_png_compare_common.h"

namespace bench_png_compare_runner {

bench_png_compare_common::ResultRow benchmark_one(
    const bench_png_compare_common::EvalImage& img,
    const bench_png_compare_common::Args& args
);

} // namespace bench_png_compare_runner
