cmake_minimum_required(VERSION 3.16)
project(HakoNyans VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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
include_directories(${CMAKE_SOURCE_DIR}/include)

# === Core library ===
# (sources will be added as implementation progresses)
# add_library(hakonyans
#   src/core/...
#   src/codec/...
#   src/entropy/nyans_p/...
#   src/platform/...
# )

# === SIMD sources (conditional) ===
# if(HAKONYANS_ARCH_X86 AND HAKONYANS_ENABLE_AVX2)
#   target_sources(hakonyans PRIVATE src/simd/x86_avx2/rans_decode_avx2.cpp)
#   set_source_files_properties(src/simd/x86_avx2/rans_decode_avx2.cpp
#     PROPERTIES COMPILE_FLAGS "-mavx2")
# endif()

# === Tools ===
# if(HAKONYANS_BUILD_TOOLS)
#   add_executable(hakonyans_cli tools/hakonyans_cli.cpp)
#   target_link_libraries(hakonyans_cli hakonyans)
# endif()

# === Benchmarks ===
# if(HAKONYANS_BUILD_BENCH)
#   add_executable(bench_entropy bench/bench_entropy.cpp)
#   target_link_libraries(bench_entropy hakonyans)
# endif()

# === Tests ===
# if(HAKONYANS_BUILD_TESTS)
#   enable_testing()
#   add_executable(test_roundtrip tests/test_roundtrip.cpp)
#   target_link_libraries(test_roundtrip hakonyans)
#   add_test(NAME roundtrip COMMAND test_roundtrip)
# endif()

message(STATUS "HakoNyans v${PROJECT_VERSION}")
message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  AVX2: ${HAKONYANS_ENABLE_AVX2}")
message(STATUS "  AVX-512: ${HAKONYANS_ENABLE_AVX512}")
message(STATUS "  NEON: ${HAKONYANS_ENABLE_NEON}")
