# Phase 9w Instructions: mode3/weighted stabilization and re-gate

## Goal
Keep `natural mode3 + weighted predictor` code path, but make it diagnosable and gate-safe.

## Scope
- `src/codec/lossless_filter_rows.h`
- `src/codec/lossless_mode_select.h`
- `src/codec/lossless_natural_route.h`
- `bench/bench_png_compare.cpp`
- `bench/bench_bit_accounting.cpp`
- `tests/test_lossless_round2.cpp`

## Tasks
1. Align filter estimator and executor
- Add weighted predictor handling (`case 6/7`) in `lossless_filter_rows.h` both for scoring and residual generation.
- Keep formulas identical to `LosslessFilter` implementation.

2. Complete mode3 telemetry
- Extend `bench_png_compare` CSV schema:
  - add `hkn_enc_route_nat_mode3_selected`
  - ensure summary print includes mode3 selected count.
- Keep existing columns unchanged, append new column(s) only.

3. Complete mode3 accounting
- Update `bench_bit_accounting` natural wrapper parser:
  - support `mode==3` header/payload split (`flat_payload_size` + `edge_payload_size`)
  - classify payload bytes into `natural_row` consistently.

4. Add regression tests
- Add natural wrapper roundtrip test that forces `mode3` selection on at least one deterministic synthetic tile.
- Add malformed mode3 wrapper decode safety test.

5. Re-gate
- Run:
  - `ctest --output-on-failure`
  - `./build/bench_png_compare --runs 3 --warmup 1 --out bench_results/phase9w_mode3_weighted_fixcheck_runs3.csv --baseline bench_results/phase9w_final.csv`
- Pass conditions:
  - fixed6 total `hkn_bytes <= 2,977,544`
  - median `PNG/HKN >= 0.2610`
  - no decode mismatch / test regressions.

## Rollback rule
If any gate fails, keep feature behind opt-in only and archive as No-Go.
