# 2026-02-14 no-go: mode2 prefetch + local-counter aggregation

## Box
- Target: `src/codec/lossless_natural_route.h`
- Function: `detail::compress_global_chain_lz(...)`
- Intent:
  - reduce inner-loop branch/store overhead by local counter aggregation
  - add light prefetch for next chain node (`prev` and source bytes)

## Result
- Status: **no-go** (reverted)
- Reason: compression gate stayed stable, but encode wall and key stage times regressed.

## Measurements
1. Multicore fixed6 (`balanced`)
- Baseline: `bench_results/phase9w_lostream_obs_final_vs_ctz_20260213_runs3.csv`
- Trial: `bench_results/phase9w_mode2_prefetch_localctr_vs_lostreamobs_20260214_runs3.csv`
- Summary:
  - `total HKN bytes`: `2,977,544 -> 2,977,544` (no change)
  - `median PNG/HKN`: `0.261035 -> 0.261035` (no change)
  - `median Enc(ms)`: `88.170 -> 90.218` (**regressed**)
  - `median plane_lo_stream(ms)`: `57.817 -> 60.390` (**regressed**)
  - `median nat_mode2(ms)`: `26.463 -> 28.561` (**regressed**)

2. Single-core fixed6 (`HAKONYANS_THREADS=1`, `taskset -c 0`)
- Baseline: `bench_results/phase9w_singlecore_after_drift_fix_vs_singlecorebase_20260213_runs3.csv`
- Trial: `bench_results/phase9w_singlecore_mode2_prefetch_localctr_vs_afterfix_20260214_runs3.csv`
- Summary:
  - `total HKN bytes`: `2,977,544 -> 2,977,544` (no change)
  - `median PNG/HKN`: `0.261035 -> 0.261035` (no change)
  - `median Enc(ms)`: `190.005 -> 202.837` (**regressed**)
  - `median plane_route_comp(ms)`: `59.676 -> 68.157` (**regressed**)
  - `median nat_mode2(ms)`: `22.952 -> 24.746` (**regressed**)

## Decision
- Revert patch and keep previous mode2 implementation.
- Keep this note as no-go history.
