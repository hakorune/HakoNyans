# 2026-02-13 mode2 Pipeline Limit Gating (No-Go)

## Scope
- target: `src/codec/lossless_natural_route.h`
- trial: add pipeline-path mode2 feasibility gate using mode0-derived size limit

## Goal
- Skip mode2 build earlier when wrapper-size lower-bound cannot satisfy
  `bias_permille` selection gate.

## Result
- size invariants: preserved
  - `total HKN bytes = 2,977,544` (unchanged)
  - `median PNG/HKN = 0.2610` (unchanged)
- but route wall-time regressed:
  - `hkn_enc_plane_route_ms`: `+7.012 / +9.870` (vs baseline)
  - `hkn_enc_ms`: `+21.248 / +13.034` (vs baseline)

Root cause:
- pipeline worker waited for mode0-derived limit before mode2 start, reducing
  overlap (`mode0` vs `mode2`) and hurting wall-clock despite mode2 inner time
  decrease.

## Artifacts
- baseline:
  - `bench_results/phase9w_mode2_ctz_balanced_20260213_runs3_rerun2.csv`
- trial:
  - `bench_results/phase9w_pipeline_mode2_limit_vs_ctz_20260213_runs3.csv`
  - `bench_results/phase9w_pipeline_mode2_limit_vs_ctz_20260213_runs3_rerun.csv`

## Decision
- no-go
- reverted from code
