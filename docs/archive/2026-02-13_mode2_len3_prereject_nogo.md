# 2026-02-13: mode2 len3-distance prereject (No-Go)

## Objective
- Speed up `route_natural` mode2 by rejecting impossible `len==3` candidates
  before `match_len_from(...)`.

## Scope
- `src/codec/lossless_natural_route.h`
  - `compress_global_chain_lz(...)` inner loop
- Non-scope:
  - format/bitstream changes
  - decode path

## Change
- For `dist > min_dist_len3` candidates:
  - if `pos+3 >= src_size`, reject immediately
  - if 4th byte mismatches, reject immediately

## Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

## Bench Artifacts
- Baseline:
  - `bench_results/phase9w_mode2_counters_balanced_20260213_runs3.csv`
- Trial:
  - `bench_results/phase9w_mode2_len3_prereject_balanced_20260213_runs3.csv`
  - `bench_results/phase9w_mode2_len3_prereject_balanced_20260213_runs3_rerun.csv`

## Result
- Invariants: passed
  - `total HKN bytes`: unchanged (`2,977,544`)
  - `median PNG/HKN`: unchanged (`0.2610`)
- Stage target did not improve:
  - `median nat_mode2(ms)`: `+1.203` (run1), `+1.208` (rerun)
  - `median route_natural(ms)`: `+0.965` (run1), `+0.676` (rerun)
- Wall clock had host-noise-sensitive improvements, but target hotspot itself
  regressed.

## Decision
- No-Go (not promoted).
- Reverted implementation and moved to next candidate:
  `match_len_from` low-level speedup (`XOR + ctz` style).
