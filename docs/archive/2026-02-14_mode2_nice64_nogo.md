# 2026-02-14: mode2 nice_length=64 cutoff (No-Go)

## Objective
- Reduce `route_natural mode2` chain-search cost with a zlib-style
  "good-enough match" early stop.

## Scope
- `src/codec/lossless_natural_route.h`
  - `GlobalChainLzParams::nice_length`
  - `compress_global_chain_lz(...)` chain-loop early break (`best_len >= nice_length`)
- `src/codec/lossless_mode_debug_stats.h`
  - add `natural_row_mode2_lz_nice_cutoff_hits`
- `bench/bench_png_compare.cpp`
  - add CSV/stat output for `hkn_enc_route_nat_mode2_lz_nice_cutoff_hits`

## Trial Config
- `HKN_LZ_NICE_LENGTH=64` (default at trial time)
- fixed6, single-core, `runs=3`, `warmup=1`

## Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

## Bench Artifact
- Baseline:
  - `bench_results/phase9w_singlecore_lostream_lbcut_obs_20260214_runs3.csv`
- Trial:
  - `bench_results/phase9w_singlecore_mode2_nice64_vs_lostreamlb_20260214_runs3.csv`

## Result
- Hot-path counters improved:
  - `nat_mode2_lz chain_steps`: `3,530,143 -> 3,354,758`
  - `nat_mode2_lz nice_cutoff_hits`: `5,044`
- Size gate failed:
  - `kodim01`: `+6,907 B`
  - `hd_01`: `+237 B`
  - fixed6 total: `+7,144 B`

## Decision
- No-Go (not promoted as default behavior).
- Keep the infrastructure and telemetry, but default `nice_length` was reset to `255`
  and future tuning is env-driven only.
