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
- report `natural_row_selected`, `gain_bytes`, `loss_bytes` for audit
