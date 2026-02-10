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

# Detect architecture
include(CheckCXXCompilerFlag)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(HAKONYANS_ARCH_X86 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
  set(HAKONYANS_ARCH_ARM TRUE)
endif()

# Include paths
include_directories(${CMAKE_SOURCE_DIR})

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
endif()

# === Benchmarks ===
if(HAKONYANS_BUILD_BENCH)
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
