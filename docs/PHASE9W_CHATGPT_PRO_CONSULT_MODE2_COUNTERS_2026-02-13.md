# Phase 9w ChatGPT Pro Consult (mode2 counters)

Date: 2026-02-13  
Target: `route_natural` encode hotspot (`mode2` global-chain LZ)

## Hard Constraints
- Lossless only
- Bitstream format compatibility
- Compression non-regression as default gate (`balanced` lane)
- C++17, small incremental mergeable diffs

## Current Baseline (fixed 6, runs=3, warmup=1, `--preset balanced`)
- CSV: `bench_results/phase9w_mode2_counters_balanced_20260213_runs3.csv`
- `median PNG/HKN`: `0.2610`
- `total HKN bytes`: `2,977,544`
- `median Enc(ms)`: `111.944`
- `median Dec(ms)`: `6.144`

## New mode2 LZ Observations (median over fixed 6)
- `nat_mode2_lz calls/src/out`: `1 / 3,253,248 / 764,271`
- `nat_mode2_lz match/literal bytes`: `3,203,428 / 159`
- `nat_mode2_lz matches`: `177,906`
- `nat_mode2_lz chain/depth/maxlen/len3rej`:
  - `chain_steps=3,530,143`
  - `depth_limit_hits=68,325`
  - `early_maxlen_hits=2,303`
  - `len3_reject_dist=38,993`

## Per-image Snapshot (from same CSV)
| image | route_natural(ms) | mode2(ms) | mode2 src -> out | chain_steps | len3_reject_dist | selected/candidates |
|---|---:|---:|---:|---:|---:|---:|
| kodim01 | 7.418 | 4.559 | 1,572,864 -> 39,241 | 252,673 | 4,626 | 2/2 |
| kodim02 | 5.734 | 2.354 | 2,359,296 -> 38,024 | 17,577 | 323 | 0/3 |
| hd_01 | 101.546 | 81.460 | 12,441,600 -> 1,612,332 | 12,301,734 | 73,360 | 1/3 |
| nature_01 | 61.940 | 53.175 | 4,147,200 -> 1,489,301 | 6,807,614 | 997,018 | 0/1 |
| nature_02 | 74.399 | 64.934 | 4,147,200 -> 1,706,859 | 9,863,133 | 1,519,633 | 0/1 |

## Code Scope
- `src/codec/lossless_natural_route.h`
  - `compress_global_chain_lz(...)`
  - `encode_plane_lossless_natural_row_tile_padded(...)`
- `src/codec/lossless_mode_debug_stats.h`
  - mode2 LZ counters
- `bench/bench_png_compare.cpp`
  - counter export/aggregation

## Ask to ChatGPT Pro
Please propose top-3 optimizations for `mode2` that satisfy all hard constraints.

For each proposal, provide:
1. Expected speed impact range on `mode2` and total encode wall.
2. Why it matches the observed counters above.
3. Minimal-diff implementation plan (exact functions/files).
4. Gate checks and rollback criteria.
5. Recommended execution order (which one first and why).

Please prioritize ideas that reduce:
- `chain_steps` cost on `hd_01` / `nature_*`
- wasted `len=3` candidate checks rejected by distance
- full-cost searches when mode2 is unlikely to beat current best
