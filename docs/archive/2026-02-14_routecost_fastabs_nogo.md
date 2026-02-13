# 2026-02-14 no-go: route_natural cost-loop fast-abs substitution

## Box
- Target: `src/codec/lossless_natural_route.h`
- Intent:
  - replace `std::abs` in mode0/mode1-prep cost loops with lighter integer abs
  - keep predictor decision and payload format unchanged

## Result
- Status: **no-go** (reverted)
- Reason: single-core encode wall regressed in fixed6 gate.

## Measurements
- Baseline:
  - `bench_results/phase9w_singlecore_blockclass_step2_scalar_final_vs_afterfix_20260214_runs3.csv`
- Trial:
  - `bench_results/phase9w_singlecore_routecost_fastabs_vs_step2_20260214_runs3.csv`

Median comparison:
- `Enc(ms)`: `176.646 -> 187.400` (**regressed**)
- `plane_route_comp(ms)`: `59.212 -> 66.875` (**regressed**)
- `total HKN bytes`: unchanged (`2,977,544`)
- `median PNG/HKN`: unchanged (`0.261035`)

## Decision
- Revert patch and keep prior implementation.

