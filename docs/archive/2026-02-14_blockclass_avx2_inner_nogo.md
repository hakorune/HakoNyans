# 2026-02-14 no-go: block_class AVX2 inner-row stats fast-path

## Box
- Target: `src/codec/lossless_block_classifier.h`
- Intent:
  - add AVX2 path for 8x8 row stats accumulation (sum / sum_sq) in block evaluation
  - keep scalar behavior for transitions/unique/palette/copy decisions
  - CPU gate: x86 + AVX2 runtime support (`HKN_BLOCK_CLASS_AVX2`)

## Result
- Status: **no-go** (reverted)
- Reason: compared with Step2-only scalar optimization, AVX2 path regressed both
  single-core and multicore encode medians.

## Measurements
### Single-core (`HAKONYANS_THREADS=1`, `taskset -c 0`, fixed6, runs=3,warmup=1)
- Baseline: `bench_results/phase9w_singlecore_after_drift_fix_vs_singlecorebase_20260213_runs3.csv`
- Step2-only:
  - `bench_results/phase9w_singlecore_blockclass_lut_scalar_vs_afterfix_20260214_runs3.csv`
  - `bench_results/phase9w_singlecore_blockclass_lut_scalar_vs_afterfix_20260214_runs3_rerun.csv`
- Step2+AVX2:
  - `bench_results/phase9w_singlecore_blockclass_step23_avx2_vs_afterfix_20260214_runs3.csv`

Median comparison:
- `Enc(ms)`: Step2-only `173.276~178.046` vs Step2+AVX2 `185.736`
- `plane_block_class(ms)`: Step2-only `26.873~27.475` vs Step2+AVX2 `28.332`
- `total HKN bytes`: unchanged (`2,977,544`)
- `median PNG/HKN`: unchanged (`0.261035`)

### Multicore (fixed6, runs=3,warmup=1)
- Baseline: `bench_results/phase9w_lostream_obs_final_vs_ctz_20260213_runs3.csv`
- Step2-only:
  - `bench_results/phase9w_blockclass_lut_scalar_vs_lostreamobs_20260214_runs3.csv`
  - `bench_results/phase9w_blockclass_lut_scalar_vs_lostreamobs_20260214_runs3_rerun.csv`
- Step2+AVX2:
  - `bench_results/phase9w_blockclass_step23_avx2_vs_lostreamobs_20260214_runs3.csv`

Median comparison:
- `Enc(ms)`: Step2-only `85.449~92.367` vs Step2+AVX2 `91.082`
- `plane_block_class(ms)`: Step2-only `20.004~20.741` vs Step2+AVX2 `22.075`
- size/ratio invariants unchanged.

## Decision
- Revert AVX2 block-class path.
- Keep Step2 scalar LUT optimization only.
