# PHASE 9X Current Goals (2026-02-14)

## Scope
- Target: Lossless only (`preset=max` for aggressive improvements, `preset=balanced` as non-regression guard)
- Fixed eval set: `kodim01`, `kodim02`, `kodim03`, `hd_01`, `nature_01`, `nature_02`
- Baseline command:
  - `HAKONYANS_THREADS=1 ./build/bench_png_compare --preset max --runs 3 --warmup 1`

## Current Baseline Snapshot
- `median(PNG/HKN)`: `0.26086`
- `total_hkn_bytes`: `2,956,913`
- Key ratios:
  - `hd_01`: `0.01236` (worst case)
  - `kodim03`: `0.32566`
  - `nature_01`: `1.57708`
  - `nature_02`: `1.44693`

## Priority Order

### P1. Worst-case rescue (`hd_01`)
- Goal:
  - Raise `hd_01` PNG/HKN from `0.012` to `>= 0.020` (phase target)
- Why first:
  - Median and perceived quality of result set are dragged down most by this single outlier.
- Immediate work:
  - Deep accounting on `hd_01` (`block_types`, `filter_ids`, `filter_lo/hi`, route reject reasons)
  - Remove avoidable overhead before algorithm swaps.
  - Observation command:
    - `python3 tools/observe_lossless_hotspots.py --image test_images/kodak/hd_01.ppm --preset max --runs 3 --warmup 1 --out docs/archive/2026-02-14_hd01_hotspot_report.md`
  - No-go logged:
    - Mode7 mixed-context CDF (`docs/archive/2026-02-14_mode7_mixed_ctx_cdf_trial.md`)
    - Outcome: selected 0 / compression unchanged on fixed6.

### P2. Photo decode speed
- Goal:
  - Photo decode median to `<= 30ms` on fixed6 max runs (current nature class remains too slow).
- Why second:
  - Photo compression advantage already exists; speed closes product gap.
- Immediate work:
  - Focus `plane_filter_lo` and `plane_reconstruct` hot path kernels (SIMD/branch/layout).

### P3. Kodak gap closure (`kodim01-03`)
- Goal:
  - `kodim03` first milestone: `-5KB` from current baseline.
  - second milestone: `-15KB`.
- Why third:
  - Requires combined work (LZ quality + token modeling); Mode6 wrapper alone is insufficient.
- Immediate work:
  - Treat as joint track: LZ match quality improvements + token-aware entropy coding.

## Hard Gates (Every PR)
- `ctest --test-dir build --output-on-failure`: `17/17 PASS`
- `preset=balanced` non-regression:
  - total bytes non-regression
  - median PNG/HKN non-regression
  - Enc/Dec non-regression
- No-Go discipline:
  - Archive failed experiments under `docs/archive/` with parameters and verdict.

## Mode Governance (Filter Lo)
- Keep `balanced` stable: experiment modes are opt-in/off by default until promoted.
- Promotion criteria:
  - fixed6 selected count is non-zero
  - fixed6 total bytes improves
  - fixed6 median PNG/HKN is non-regression
  - full tests pass (including malformed decode path)
- Current fixed6 usage:
  - active: mode `0/1/2/3/4/5`
  - experimental no-go: mode `6/7`
