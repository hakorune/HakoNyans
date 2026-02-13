# 2026-02-13: plane_lo_stream pruning trial (No-Go)

## Objective
- Improve `hkn_enc_plane_lo_stream_ms` with low-risk pruning and setup cleanups.

## Scope
- Trial target:
  - `src/codec/lossless_filter_lo_codec.h`
- Non-scope:
  - format/bitstream changes
  - decode path

## Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

## Bench Artifacts
- Baseline:
  - `bench_results/phase9w_lo_stream_opt_baseline_20260213_runs3.csv`
- Trial candidates:
  - `bench_results/phase9w_lo_stream_opt_candidate_20260213_runs3.csv`
  - `bench_results/phase9w_lo_stream_opt_candidate_20260213_runs3_rerun.csv`
  - `bench_results/phase9w_lo_stream_opt_candidate_v2_20260213_runs3.csv`
  - `bench_results/phase9w_lo_stream_opt_candidate_v3_20260213_runs3.csv`

## Result
- Compression invariants preserved (`total HKN bytes` and `median PNG/HKN` unchanged).
- Stage target (`hkn_enc_plane_lo_stream_ms`) did not show a stable improvement across reruns.
- Some runs showed regression-scale movement; no robust promote signal.

## Decision
- No-Go.
- Trial implementation was reverted from mainline.
