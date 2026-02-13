# 2026-02-14 no-go: lo_stream mode3 branch-reduction rewrite

## Box
- Target: `src/codec/lossless_filter_lo_codec.h`
- Intent:
  - keep predictor choice semantics and early-break behavior
  - reduce branch cost in mode3 row predictor eval and residual generation

## Result
- Status: **no-go** (reverted)
- Reason: fixed6 single-core encode wall regressed versus promoted Step2 baseline.

## Measurements
- Baseline:
  - `bench_results/phase9w_singlecore_blockclass_step2_scalar_final_vs_afterfix_20260214_runs3.csv`
- Trial:
  - `bench_results/phase9w_singlecore_lostream_mode3_branchcut_vs_step2_20260214_runs3.csv`

Median comparison:
- `Enc(ms)`: `176.646 -> 183.268` (**regressed**)
- `plane_lo_stream(ms)`: `58.312 -> 57.735` (slight stage gain, but wall lost)
- `plane_route_comp(ms)`: `59.212 -> 67.266` (**regressed**)
- `total HKN bytes`: unchanged (`2,977,544`)
- `median PNG/HKN`: unchanged (`0.261035`)

## Decision
- Revert patch and keep baseline behavior.

