#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../src/codec/decode.h"
#include "../src/codec/encode.h"
#include "bench_png_compare_common.h"
#include "bench_png_compare_runner.h"
#include "png_wrapper.h"
#include "ppm_loader.h"

namespace bench_png_compare_runner {
using namespace hakonyans;
using namespace bench_png_compare_common;

ResultRow benchmark_one(const EvalImage& img, const Args& args) {
    ResultRow row;
    row.image_id = img.rel_path;
    row.image_name = img.name;

    const std::string full_path = args.base_dir + "/" + img.rel_path;
    auto ppm = load_ppm(full_path);
    row.width = ppm.width;
    row.height = ppm.height;

    std::cout << "[RUN] " << img.name << " ... " << std::flush;

    #include "bench_png_compare_runner_samples_decl.inc"

    #include "bench_png_compare_runner_sample_collect.inc"

    #include "bench_png_compare_runner_row_finalize.inc"
}

} // namespace bench_png_compare_runner
