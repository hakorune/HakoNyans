# HakoNyans Product Positioning

**Date**: 2026-02-13  
**Scope**: Strategic positioning and measurable goals

## 1. North Star

HakoNyans targets the space of:

- Fast decode on commodity CPU
- Practical compression for screen/anime/mixed content
- Lightweight and maintainable implementation

This is not a direct "maximum compression ratio at any cost" race.

## 2. Competitive Position

### Against WebP

- Axis: UI quality and decode speed
- Position: cleaner edges/text + stronger parallel decode path
- Near-term lever (P1): CfL tuning and robust mode selection

### Against JPEG XL

- Axis: implementation complexity and deployability
- Position: simpler architecture ("box theory"), easier embedding
- Near-term lever (P1): preserve decode advantage while improving ratio

### Against x265/HEVC-family mindset

- Axis: absolute compression efficiency
- Position: do not compete head-on first
- Strategy: win with balanced ratio + latency + implementation cost

## 3. Target Segments

### A. Remote desktop / screen sharing

- Need: low-latency decode, stable text/UI quality
- P1 focus: CfL tuning, MED predictor

### B. Game / app asset distribution

- Need: fast load and reasonable package size
- P1 focus: tile match / LZ-style gains without decode slowdown

### C. Anime / manga delivery

- Need: flat-color fidelity and repeated-pattern efficiency
- P1 focus: palette/copy strengths + chroma prediction tuning

## 4. KPI Baseline and Stage Goals

## KPI (core)

- Decode time (Full HD, bench_decode)
- Compression ratio by category (UI / Anime / Photo / Game)
- Quality metric consistency (PSNR/SSIM/MS-SSIM in RD evaluation)
- Compatibility and reliability (tests + cross-version decode)

## Current baseline (as of 2026-02-13)

- Full HD decode: ~19.2 ms class
- 17/17 tests passing
- CfL safety update: tile-level on/off choose-smaller and compatibility maintained

### Phase 9w lossless fixed-6 anchor (as of 2026-02-13)

- source: `bench_results/tmp_isolate_default_step12_20260213_runs5.csv`
- PNG reference:
  - median Enc: `108.231 ms`
  - median Dec: `6.409 ms`
  - total bytes (fixed-6): `2,864,560`
- current HKN:
  - median Enc: `102.309 ms`
  - median Dec: `8.626 ms`
  - total bytes (fixed-6): `2,977,544`

## Stage goals

### P1 (next)

- Decode: **<= PNG median 6.409 ms** on fixed-6 benchmark
- Encode: **<= PNG median 108.231 ms** on fixed-6 benchmark
- Size: **<= PNG total 2,864,560 bytes** on fixed-6 benchmark
- Compression:
  - Photo: **-5% to -15%** via MED-class prediction improvements
  - UI/Anime: **-3% to -7%** via CfL tuning and mode policy
  - Repetitive assets: **-5% to -10%** via tile match/LZ-style tools

### P2

- Decode: **12-15 ms** class
- Compression: stabilize gains across mixed-content datasets

### P3 (stretch)

- Decode: **< 10 ms** class for selected profiles

## 5. Guardrails

- Do not land ratio features that break decode latency budget
- Maintain backward compatibility unless explicit format bump is approved
- Prefer per-tile/per-tool fallback when optimization can regress some categories
- Keep AGENTS.md for workflow rules only; strategy lives in this document

## 6. Decision Policy

A feature is accepted when all conditions hold:

- Compression improves in target category with reproducible benchmark
- Decode regression stays within budget (default <= +5%)
- Test suite remains green
- Compatibility behavior is documented and validated
