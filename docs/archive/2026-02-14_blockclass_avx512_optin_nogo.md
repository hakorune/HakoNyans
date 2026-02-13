# 2026-02-14 no-go: block_class AVX512 opt-in row-stats path

## Box
- Target: `src/codec/lossless_block_classifier.h`
- Intent:
  - add `HKN_EXPERIMENTAL_AVX512=1` opt-in path
  - AVX512 row stats accumulation (sum / sum_sq) for 8x8 block evaluation
  - keep default behavior unchanged when env is unset

## Result
- Status: **no-go** (reverted)
- Reason: main target (single-core encode) did not improve versus Step2 scalar optimization.

## Measurements
### Single-core (primary gate)
- Baseline (Step2): `bench_results/phase9w_singlecore_blockclass_step2_scalar_final_vs_afterfix_20260214_runs3.csv`
- Trial (Step3 AVX512): `bench_results/phase9w_singlecore_blockclass_step3_avx512optin_vs_step2_20260214_runs3.csv`

Median comparison:
- `Enc(ms)`: `176.646 -> 177.883` (**regressed**)
- `plane_block_class(ms)`: `27.356 -> 27.706` (**regressed**)
- `total HKN bytes`: unchanged (`2,977,544`)
- `median PNG/HKN`: unchanged (`0.261035`)

### Multicore (reference only)
- Baseline (Step2): `bench_results/phase9w_blockclass_step2_scalar_final_vs_lostreamobs_20260214_runs3.csv`
- Trial (Step3 AVX512): `bench_results/phase9w_blockclass_step3_avx512optin_vs_step2_20260214_runs3.csv`

Median comparison:
- `Enc(ms)`: `92.938 -> 87.832` (improved)
- `Dec(ms)`: `6.259 -> 6.488` (slightly worse)
- single-core優先ルールにより採用せず。

## Decision
- Revert AVX512 opt-in path.
- Keep Step2 scalar optimization as current promoted state.
