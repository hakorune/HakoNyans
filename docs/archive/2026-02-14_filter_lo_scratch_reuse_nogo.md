# 2026-02-14 no-go: filter_lo scratch reuse / mode4 buffer rewrite

## Box
- Target: `src/codec/lossless_filter_lo_codec.h`
- Intent: reduce allocation/copy overhead in `encode_filter_lo_stream()`
  - `thread_local` scratch vectors
  - mode4 context split rewrite (`resize once + memcpy by write offset`)
  - wrapper header write helper

## Result
- Status: **no-go** (reverted)
- Reason: size gate stayed stable, but encode wall regressed in both measurements.

## Measurements
1. Multicore fixed6 (`balanced`)
- Baseline: `bench_results/phase9w_lostream_obs_final_vs_ctz_20260213_runs3.csv`
- Trial: `bench_results/phase9w_lostream_scratchopt_vs_lostreamobs_20260214_runs3.csv`
- Summary:
  - `total HKN bytes`: `2,977,544 -> 2,977,544` (no change)
  - `median PNG/HKN`: `0.261035 -> 0.261035` (no change)
  - `median Enc(ms)`: `88.170 -> 90.098` (**regressed**)
  - `median plane_lo_stream(ms)`: `57.817 -> 58.454` (**regressed**)

2. Single-core fixed6 (`HAKONYANS_THREADS=1`, `taskset -c 0`)
- Baseline: `bench_results/phase9w_singlecore_after_drift_fix_vs_singlecorebase_20260213_runs3.csv`
- Trial: `bench_results/phase9w_singlecore_lostream_scratchopt_vs_afterfix_20260214_runs3.csv`
- Summary:
  - `total HKN bytes`: `2,977,544 -> 2,977,544` (no change)
  - `median PNG/HKN`: `0.261035 -> 0.261035` (no change)
  - `median Enc(ms)`: `190.005 -> 197.674` (**regressed**)
  - `median plane_lo_stream(ms)`: `58.132 -> 56.890` (improved)
  - `median plane_route_comp(ms)`: `59.676 -> 67.922` (**regressed**)

## Decision
- Revert patch and keep baseline code.
- Keep this note as no-go history.
