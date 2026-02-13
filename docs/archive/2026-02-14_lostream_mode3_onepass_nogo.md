# 2026-02-14 no-go: lo_stream mode3 one-pass predictor cost

## Box
- Target: `src/codec/lossless_filter_lo_codec.h`
- Change: mode3 predictor selection cost loop
  - before: predictor-by-predictor (`p=0..3`) scan with early break
  - trial: one-pass accumulation of cost0/cost1/cost2/cost3 per row
- Goal: reduce mode3 eval compute while preserving exact predictor choice.

## Result
- Status: **no-go** (reverted)
- Reason: size/ratio invariants held, but single-core encode regressed.

## Measurements
1. Multicore fixed6 (`balanced`)
- Baseline: `bench_results/phase9w_lostream_obs_final_vs_ctz_20260213_runs3.csv`
- Trial: `bench_results/phase9w_lostream_mode3_onepass_vs_lostreamobs_20260214_runs3.csv`
- Summary:
  - `total HKN bytes`: `2,977,544 -> 2,977,544` (no change)
  - `median PNG/HKN`: `0.261035 -> 0.261035` (no change)
  - `median Enc(ms)`: `88.170 -> 86.690` (improved)
  - `median plane_lo_stream(ms)`: `57.817 -> 60.379` (**regressed**)

2. Single-core fixed6 (`HAKONYANS_THREADS=1`, `taskset -c 0`)
- Baseline: `bench_results/phase9w_singlecore_after_drift_fix_vs_singlecorebase_20260213_runs3.csv`
- Trial: `bench_results/phase9w_singlecore_lostream_mode3_onepass_vs_afterfix_20260214_runs3.csv`
- Summary:
  - `total HKN bytes`: `2,977,544 -> 2,977,544` (no change)
  - `median PNG/HKN`: `0.261035 -> 0.261035` (no change)
  - `median Enc(ms)`: `190.005 -> 195.356` (**regressed**)
  - `median plane_lo_stream(ms)`: `58.132 -> 59.866` (**regressed**)
  - `median plane_route_comp(ms)`: `59.676 -> 62.229` (**regressed**)

## Decision
- Revert patch and keep baseline.
- Keep this note as no-go history.
