# Phase 9w: Natural Route LZ Parameter Sweep Instructions

## Goal
Tune Natural route LZ parameters to improve fixed 6-image Natural benchmark:

- `test_images/kodak/kodim01.ppm`
- `test_images/kodak/kodim02.ppm`
- `test_images/kodak/kodim03.ppm`
- `test_images/kodak/hd_01.ppm`
- `test_images/photo/nature_01.ppm`
- `test_images/photo/nature_02.ppm`

Primary objective:
- maximize median `PNG_bytes / HKN_bytes`

Tie-break objectives:
- maximize Kodak mean (`kodim01/02/03`) of `PNG/HKN`
- minimize total HKN bytes over fixed 6 images
- avoid decode regressions (`Dec(ms)` median)

## Runtime knobs
Natural route now accepts runtime env overrides:

- `HKN_LZ_CHAIN_DEPTH` (default `32`, range `1..128`)
- `HKN_LZ_WINDOW_SIZE` (default `65535`, range `1024..65535`)
- `HKN_LZ_MIN_DIST_LEN3` (default `128`, range `0..65535`)
- `HKN_LZ_BIAS_PERMILLE` (default `990`, range `900..1100`)

Natural prefilter thresholds (already tuned) can also be overridden:

- `HKN_NATURAL_UNIQUE_MIN`
- `HKN_NATURAL_AVG_RUN_MAX`
- `HKN_NATURAL_MAD_MIN`
- `HKN_NATURAL_ENTROPY_MIN`

## Sweep script
Use:

```bash
python3 tools/sweep_natural_route_params.py
```

Output:
- `bench_results/phase9w_routeparam_sweep_raw.csv`

CSV contains:
- parameter tuple
- `median_ratio`
- `kodim_mean_ratio`
- `total_hkn_bytes`
- `median_dec_ms`
- per-image ratio/bytes/dec/natural_selected/gain/loss

## Verification flow
1. Build and tests:

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

2. Run fixed benchmark with chosen params:

```bash
HKN_LZ_CHAIN_DEPTH=32 \
HKN_LZ_WINDOW_SIZE=65535 \
HKN_LZ_MIN_DIST_LEN3=128 \
HKN_LZ_BIAS_PERMILLE=990 \
./build/bench_png_compare --runs 3 --warmup 1 --out bench_results/phase9w_final.csv
```

3. Compare against baseline:

```bash
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --baseline bench_results/phase9w_current.csv \
  --out bench_results/phase9w_final_ab.csv
```

## Acceptance criteria
- `ctest` 17/17 PASS
- no regression in median `PNG/HKN`
- improved or equal total HKN bytes on fixed 6 images
- no regression in median `Enc(ms)` and `Dec(ms)` (wall-clock)
- report `natural_row_selected`, `gain_bytes`, `loss_bytes` for audit

Fixed policy reference:
- `docs/PHASE9W_SPEED_SIZE_BALANCE_POLICY.md`

## Speed front comparison (HKN vs PNG)

`bench_png_compare` now reports size + speed in one run:

```bash
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --out bench_results/phase9w_speed_stage_profile.csv
```

Printed summary:
- per-image `Enc(ms HKN/PNG)` and `Dec(ms HKN/PNG)`
- median `Enc(ms)` and `Dec(ms)` for HKN/PNG
- `HKN Stage Breakdown (median over fixed 6)`
- stage lines are `cpu_sum`; use wall-clock lines for go/no-go

CSV (same file) keeps old columns and appends stage metrics:
- top-level: `hkn_enc_ms`, `hkn_dec_ms`, `png_enc_ms`, `png_dec_ms`
- encode stages: `hkn_enc_rgb_to_ycocg_ms`, `hkn_enc_plane_*`, `hkn_enc_container_pack_ms`
- decode stages: `hkn_dec_header_ms`, `hkn_dec_plane_*`, `hkn_dec_ycocg_to_rgb_ms`

## Current hotspot snapshot (2026-02-12)

From `bench_results/phase9w_speed_stage_profile_after_route_filter_parallel.csv`:
- median `Enc(ms)` HKN/PNG: `168.179 / 109.246` (`HKN/PNG=1.539`)
- median `Dec(ms)` HKN/PNG: `18.511 / 6.472` (`HKN/PNG=2.860`)
- `cpu_sum / wall`:
  - Encode: `1.915`
  - Decode: `1.862`

Encode hotspot (median):
- `plane_route_comp`: `135.876 ms`
- `plane_block_class`: `84.311 ms`
- `plane_lo_stream`: `58.921 ms`

Decode hotspot (median):
- `plane_filter_lo`: `15.176 ms`
- `plane_reconstruct`: `6.208 ms`
- `plane_filter_hi`: `2.424 ms`
