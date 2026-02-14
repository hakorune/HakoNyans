cmake_minimum_required(VERSION 3.16)
project(HakoNyans VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(HAKONYANS_BUILD_TOOLS "Build CLI tools" ON)
option(HAKONYANS_BUILD_BENCH "Build benchmarks" ON)
option(HAKONYANS_BUILD_TESTS "Build tests" ON)
option(HAKONYANS_BUILD_FUZZ  "Build fuzz targets" OFF)
option(HAKONYANS_ENABLE_AVX2 "Enable AVX2 SIMD" ON)
option(HAKONYANS_ENABLE_AVX512 "Enable AVX-512 SIMD" OFF)
option(HAKONYANS_ENABLE_NEON "Enable ARM NEON SIMD" OFF)

# Band-group CDF split points (zigzag index)
set(HAKONYANS_BAND_LOW_END "15" CACHE STRING "LOW band end zigzag index (1..61)")
set(HAKONYANS_BAND_MID_END "31" CACHE STRING "MID band end zigzag index (LOW+1..62)")
if(NOT HAKONYANS_BAND_LOW_END MATCHES "^[0-9]+$" OR NOT HAKONYANS_BAND_MID_END MATCHES "^[0-9]+$")
  message(FATAL_ERROR "HAKONYANS_BAND_LOW_END and HAKONYANS_BAND_MID_END must be positive integers")
endif()
if(HAKONYANS_BAND_LOW_END LESS 1 OR HAKONYANS_BAND_LOW_END GREATER 61)
  message(FATAL_ERROR "HAKONYANS_BAND_LOW_END must be in [1, 61]")
endif()
if(HAKONYANS_BAND_MID_END LESS 2 OR HAKONYANS_BAND_MID_END GREATER 62)
  message(FATAL_ERROR "HAKONYANS_BAND_MID_END must be in [2, 62]")
endif()
if(NOT HAKONYANS_BAND_LOW_END LESS HAKONYANS_BAND_MID_END)
  message(FATAL_ERROR "HAKONYANS_BAND_LOW_END must be less than HAKONYANS_BAND_MID_END")
endif()

# Detect architecture
include(CheckCXXCompilerFlag)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(HAKONYANS_ARCH_X86 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
  set(HAKONYANS_ARCH_ARM TRUE)
endif()

# Include paths
include_directories(${CMAKE_SOURCE_DIR})
add_compile_definitions(
  HAKONYANS_BAND_LOW_END=${HAKONYANS_BAND_LOW_END}
  HAKONYANS_BAND_MID_END=${HAKONYANS_BAND_MID_END}
)

# === Tests ===
if(HAKONYANS_BUILD_TESTS)
  enable_testing()
  
  # Phase 1: Simple rANS test
  add_executable(test_rans_simple tests/test_rans_simple.cpp)
  add_test(NAME rans_simple COMMAND test_rans_simple)
  
  # Phase 2: Interleaved rANS + Tokenization test
  add_executable(test_interleaved tests/test_interleaved.cpp)
  add_test(NAME interleaved COMMAND test_interleaved)
  
  # Phase 3: Flat Interleaved + AVX2 test
  add_executable(test_avx2_rans tests/test_avx2_rans.cpp)
  target_compile_options(test_avx2_rans PRIVATE -mavx2)
  add_test(NAME avx2_rans COMMAND test_avx2_rans)
  
  # Phase 4: P-Index parallel decode test
  add_executable(test_parallel tests/test_parallel.cpp)
  target_link_libraries(test_parallel PRIVATE pthread)
  add_test(NAME parallel COMMAND test_parallel)
  
  # Phase 5: Component tests
  add_executable(test_phase5_components tests/test_phase5_components.cpp)
  add_test(NAME phase5_components COMMAND test_phase5_components)
  
  # Phase 5: Codec roundtrip tests
  add_executable(test_codec_gray tests/test_codec_gray.cpp)
  add_test(NAME codec_gray COMMAND test_codec_gray)

  add_executable(test_codec_color tests/test_codec_color.cpp)
  add_test(NAME codec_color COMMAND test_codec_color)

  add_executable(test_adaptive_quant tests/test_adaptive_quant.cpp)
  target_link_libraries(test_adaptive_quant PRIVATE pthread)
  add_test(NAME adaptive_quant COMMAND test_adaptive_quant)

  add_executable(test_420_subsampling tests/test_420_subsampling.cpp)
  target_link_libraries(test_420_subsampling PRIVATE pthread)
  add_test(NAME subsampling_420 COMMAND test_420_subsampling)

  add_executable(test_cfl tests/test_cfl.cpp)
  target_link_libraries(test_cfl PRIVATE pthread)
  add_test(NAME cfl COMMAND test_cfl)

  add_executable(test_band_cdf tests/test_band_cdf.cpp)
  target_link_libraries(test_band_cdf PRIVATE pthread)
  add_test(NAME band_cdf COMMAND test_band_cdf)

  # Screen Profile Step 1: BlockType Infrastructure
  add_executable(test_screen_profile_step1 tests/test_screen_profile_step1.cpp)
  target_link_libraries(test_screen_profile_step1 PRIVATE pthread)
  add_test(NAME screen_profile_step1 COMMAND test_screen_profile_step1)

  # Screen Profile Step 2: Palette Mode
  add_executable(test_screen_profile_step2 tests/test_screen_profile_step2.cpp)
  target_link_libraries(test_screen_profile_step2 PRIVATE pthread)
  add_test(NAME screen_profile_step2 COMMAND test_screen_profile_step2)

  # Screen Profile Step 3: Copy Mode
  add_executable(test_screen_profile_step3 tests/test_screen_profile_step3.cpp)
  target_link_libraries(test_screen_profile_step3 PRIVATE pthread)
  add_test(NAME screen_profile_step3 COMMAND test_screen_profile_step3)

  # Screen Profile Step 4: Encoder Integration
  add_executable(test_screen_profile_step4 tests/test_screen_profile_step4.cpp)
  target_link_libraries(test_screen_profile_step4 PRIVATE pthread)
  add_test(NAME screen_profile_step4 COMMAND test_screen_profile_step4)
  # Phase 8: Lossless Round 1
  add_executable(test_lossless_round1 tests/test_lossless_round1.cpp)
  target_link_libraries(test_lossless_round1 PRIVATE pthread)
  add_test(NAME lossless_round1 COMMAND test_lossless_round1)
  # Phase 8: Lossless Round 2
  add_executable(test_lossless_round2 tests/test_lossless_round2.cpp)
  target_link_libraries(test_lossless_round2 PRIVATE pthread)
  add_test(NAME lossless_round2 COMMAND test_lossless_round2)
endif()

# === Benchmarks ===
if(HAKONYANS_BUILD_BENCH)
  # Find libpng for PNG comparison benchmark
  find_package(PNG REQUIRED)
  include_directories(${PNG_INCLUDE_DIRS})

  # Entropy benchmark (Phase 2)
  add_executable(bench_entropy bench/bench_entropy.cpp)
  target_compile_options(bench_entropy PRIVATE -O3 -march=native)
  
  # Phase 3 benchmark (N=1 vs Flat vs LUT vs AVX2)
  add_executable(bench_phase3 bench/bench_phase3.cpp)
  target_compile_options(bench_phase3 PRIVATE -O3 -march=native -mavx2)

  # Phase 5.4 benchmark (Full HD Decode)
  add_executable(bench_decode bench/bench_decode.cpp)
  target_compile_options(bench_decode PRIVATE -O3 -march=native)
  target_link_libraries(bench_decode PRIVATE pthread)

  # Screen Profile benchmark
  add_executable(bench_screen_profile bench/bench_screen_profile.cpp)
  target_compile_options(bench_screen_profile PRIVATE -O3 -march=native)
  target_link_libraries(bench_screen_profile PRIVATE pthread)

  # Lossless benchmark (Phase 8)
  add_executable(bench_lossless bench/bench_lossless.cpp)
  target_compile_options(bench_lossless PRIVATE -O3 -march=native)
  target_link_libraries(bench_lossless PRIVATE pthread)

  # PNG vs HKN Lossless comparison benchmark (Phase 8b)
  add_executable(
    bench_png_compare
    bench/bench_png_compare.cpp
    bench/bench_png_compare_runner.cpp
  )
  target_compile_options(bench_png_compare PRIVATE -O3 -march=native)
  target_link_libraries(bench_png_compare PRIVATE ${PNG_LIBRARIES} pthread)

  # Bit accounting benchmark (Phase 9)
  add_executable(
    bench_bit_accounting
    bench/bench_bit_accounting.cpp
    bench/bench_bit_accounting_common.cpp
    bench/bench_bit_accounting_lossless_report.cpp
  )
  target_compile_options(bench_bit_accounting PRIVATE -O3 -march=native)
  target_link_libraries(bench_bit_accounting PRIVATE pthread)

  # Anime quality verification tool (Phase 8d)
  add_executable(bench_anime_quality bench/bench_anime_quality.cpp)
  target_compile_options(bench_anime_quality PRIVATE -O3 -march=native)
  target_link_libraries(bench_anime_quality PRIVATE ${PNG_LIBRARIES} pthread)
endif()

# === Tools ===
if(HAKONYANS_BUILD_TOOLS)
  add_executable(hakonyans tools/hakonyans_cli.cpp)
endif()

message(STATUS "HakoNyans v${PROJECT_VERSION}")
message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  AVX2: ${HAKONYANS_ENABLE_AVX2}")
message(STATUS "  AVX-512: ${HAKONYANS_ENABLE_AVX512}")
message(STATUS "  NEON: ${HAKONYANS_ENABLE_NEON}")
message(STATUS "  Tests: ${HAKONYANS_BUILD_TESTS}")
message(STATUS "  Band Split (zigzag): low<=${HAKONYANS_BAND_LOW_END}, mid<=${HAKONYANS_BAND_MID_END}, high>${HAKONYANS_BAND_MID_END}")

# Anime lossy benchmark
if(PNG_FOUND)
  if(EXISTS "${CMAKE_SOURCE_DIR}/bench/bench_anime_lossy.cpp")
    add_executable(bench_anime_lossy bench/bench_anime_lossy.cpp)
    target_compile_options(bench_anime_lossy PRIVATE -O3 -march=native)
    target_link_libraries(bench_anime_lossy PRIVATE ${PNG_LIBRARIES} pthread)
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/bench/repro_artoria.cpp")
    add_executable(repro_artoria bench/repro_artoria.cpp)
    target_compile_options(repro_artoria PRIVATE -O3 -march=native)
    target_link_libraries(repro_artoria PRIVATE ${PNG_LIBRARIES} pthread)
  endif()
endif()
