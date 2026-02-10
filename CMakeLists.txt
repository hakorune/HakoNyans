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
  
  # Simple test (no external dependencies)
  add_executable(test_rans_simple tests/test_rans_simple.cpp)
  add_test(NAME rans_simple COMMAND test_rans_simple)
endif()

message(STATUS "HakoNyans v${PROJECT_VERSION}")
message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  AVX2: ${HAKONYANS_ENABLE_AVX2}")
message(STATUS "  AVX-512: ${HAKONYANS_ENABLE_AVX512}")
message(STATUS "  NEON: ${HAKONYANS_ENABLE_NEON}")
message(STATUS "  Tests: ${HAKONYANS_BUILD_TESTS}")
