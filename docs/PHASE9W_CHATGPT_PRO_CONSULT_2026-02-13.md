# Phase 9w ChatGPT Pro Consult (2026-02-13)

## Goal
- Improve **lossless encode speed** in HakoNyans without worsening compression.
- Preserve format compatibility and decoded output.
- Keep gate strict:
  - median(PNG/HKN) non-regression
  - total HKN bytes non-regression
  - tests 17/17 pass

## Current Branch Context
- Project: HakoNyans
- Focus path: lossless route competition -> natural route (`plane_route_comp` / `route_natural`)
- Recent changes already done:
  - route-comp deep counters
  - natural mode1/mode2 duplicate-work removal
  - conditional prep parallelism

## Latest Measurement (fixed 6 images, runs=3, warmup=1)
- Baseline CSV:
  - `bench_results/tmp_next_move_natural_parallelprep_rerun.csv`
- Latest CSV:
  - `bench_results/tmp_measure_natural_deep_20260213_rerun.csv`

### Quality/size invariants
- median PNG/HKN: `0.2610` (unchanged)
- total HKN bytes: `2,977,544` (unchanged)

### Wall-clock medians
- Enc(ms) HKN/PNG: `114.150 / 108.586` (HKN/PNG=`1.051`)
- Dec(ms) HKN/PNG: `14.781 / 6.386` (HKN/PNG=`2.314`)

### Encode stage medians
- `plane_route_comp`: `63.345 ms`
  - `route_prefilter`: `0.221 ms`
  - `route_screen`: `0.000 ms`
  - `route_natural`: `52.410 ms`
    - `nat_mode0`: `18.502 ms`
    - `nat_mode1prep`: `9.910 ms`
    - `nat_pred_pack`: `0.012 ms`
    - `nat_mode1`: `13.235 ms`
    - `nat_mode2`: `32.870 ms`

### Route/natural counters (median per image)
- route compete scheduler parallel/seq/tokens: `0/3/0`
- route natural prep parallel/seq/tokens: `1/0/1`
- route natural m1/m2 parallel/seq/tokens: `1/0/1`
- route natural selected mode0/mode1/mode2: `0/0/1`
- route natural pred raw/rans: `1/0`
- route natural mode2 bias adopt/reject: `1/0`

## Interpretation
- Main remaining encode hotspot inside route-comp is `route_natural`.
- In practice, natural winner is currently mode2 in selected cases.
- Most expensive internal pieces are:
  - mode2 build (`~33 ms`)
  - mode0 build (`~18.5 ms`)
  - mode1 build (`~13.2 ms`)
  - mode1 prepare (`~9.9 ms`)

## Key Source Files
- `src/codec/lossless_natural_route.h`
- `src/codec/lossless_route_competition.h`
- `src/codec/encode.h`
- `src/codec/lossless_mode_debug_stats.h`
- `bench/bench_png_compare.cpp`

## Ask to ChatGPT Pro
Please propose **top 3 concrete implementation plans** (C++17, mergeable in steps) that can reduce `route_natural` encode time significantly while preserving compression/format behavior.

Constraints:
1. No format change.
2. No size regression (fixed-6 gate).
3. Keep code maintainable and testable.

For each plan, include:
- expected impact range
- risk level
- exact target functions/loops
- minimal patch strategy (commit-sized)
- gate checks to accept/reject

Also include:
- Any low-risk SIMD/memcpy opportunities
- Any algorithmic pruning that is guaranteed non-regressive for output size
- Recommended order of implementation (highest ROI first)
