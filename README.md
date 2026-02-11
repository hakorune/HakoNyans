# HakoNyans

[English](README.md) | [日本語](README.ja.md)

HakoNyans is an experimental image codec focused on practical decode speed, modular design ("box theory"), and competitive compression for screen/anime/photo content.

## Why HakoNyans
- Parallel entropy core (`NyANS-P`) with decode-oriented design
- Fast block pipeline with SIMD-friendly components (AAN IDCT, lightweight predictors)
- Hybrid lossless tools for screen content: `Copy`, `Palette`, `Filter`
- Clear module boundaries, reproducible benchmarks, and paper artifacts in-repo

## Current Focus
- Better size-quality tradeoff in lossy mode (CfL tuning, band models)
- Better photo performance in lossless mode (predictor and mode selection)
- Keep decode latency stable while improving compression ratio

## Build
```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

## CLI
```bash
# Encode (PPM -> HKN)
./hakonyans encode input.ppm output.hkn [quality]

# Decode (HKN -> PPM)
./hakonyans decode input.hkn output.ppm

# Show stream info
./hakonyans info input.hkn
```

## Benchmark and Repro
- Main benchmark tools are under `bench/`.
- Reproducible paper scripts and generated tables/figures are under `paper/repro/` and `paper/results/`.
- For fair comparisons, use the same image set and report:
  - file size
  - decode time in `ms`
  - quality metrics (`PSNR`, `SSIM`, optional `MS-SSIM`)

## Repository Layout
```text
src/            codec, entropy, SIMD, platform code
bench/          benchmark executables and measurement tools
tests/          unit/integration tests
docs/           implementation notes and phase instructions
paper/          paper sources, scripts, tables, result assets
```

## Contact
- X (Twitter): https://x.com/CharmNexusCore

## License
MIT License
