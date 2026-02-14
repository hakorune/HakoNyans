# Phase 9X-3: PHOTO Profile LZ_COST Filter Row Selector - Results

Date: 2026-02-14

## Implementation Summary

Successfully implemented LZ_COST filter row selection mode for PHOTO profile. This new cost model uses a lightweight LZ-based estimation to improve filter selection accuracy for natural images.

### Changes Made

1. **src/codec/lossless_filter_rows.h**
   - Added `LZCOST = 3` to `FilterRowCostModel` enum
   - Added environment variable parsing:
     - `HKN_FILTER_ROWS_COST_MODEL=lzcost`
     - `HKN_FILTER_ROWS_LZCOST_TOPK` (default: 2, range: 1-4)
     - `HKN_FILTER_ROWS_LZCOST_WINDOW` (default: 256, range: 64-1024)
     - `HKN_FILTER_ROWS_LZCOST_ENABLE_PHOTO_ONLY` (default: 1)
   - Implemented `lzcost_estimate_row()` function with uint8_t residual input
   - Added two-stage selection: BITS2 top-K selection + LZ cost evaluation

2. **src/codec/lossless_mode_debug_stats.h**
   - Added LZCOST telemetry counters:
     - `filter_rows_lzcost_eval_rows`
     - `filter_rows_lzcost_topk_sum`
     - `filter_rows_lzcost_paeth_selected`
     - `filter_rows_lzcost_med_selected`
     - `filter_rows_lzcost_ns`

3. **bench/bench_bit_accounting.cpp**
   - Added LZCOST diagnostics display

4. **bench/bench_png_compare.cpp**
   - Added CSV columns for LZCOST telemetry

5. **tests/test_lossless_round2.cpp**
   - Added 5 new tests:
     - `test_filter_rows_lzcost_roundtrip()`
     - `test_filter_rows_lzcost_deterministic()`
     - `test_filter_rows_lzcost_photo_only_disabled()`
     - `test_filter_rows_lzcost_env_default_compat()`
     - `test_csv_column_count_consistency()`

## Test Results

```
ctest: 17/17 PASS
lossless_round2: 58/58 tests passed (including 5 new LZCOST tests)
```

## Verification Commands

### Build & Test
```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

### Baseline (max, LZCOST off)
```bash
HAKONYANS_THREADS=1 ./build/bench_png_compare --preset max --runs 3 --warmup 1 \
  --out bench_results/tmp_lzcost_off.csv
```

### Trial (max, LZCOST on)
```bash
HAKONYANS_THREADS=1 HKN_FILTER_ROWS_COST_MODEL=lzcost ./build/bench_png_compare \
  --preset max --runs 3 --warmup 1 --out bench_results/tmp_lzcost_on.csv
```

### Diagnostics
```bash
HKN_FILTER_ROWS_COST_MODEL=lzcost ./build/bench_bit_accounting \
  test_images/kodak/kodim03.ppm --lossless
```

## Acceptance Criteria (Gate Conditions)

### Required (must pass)
1. ctest: 17/17 PASS - OK
2. max: total_hkn_bytes(on) <= off (no regression) - **FAIL**
   - off: 2,954,276 bytes
   - on:  2,973,200 bytes (+18,924)
3. max: median PNG/HKN(on) >= off (no regression) - **FAIL**
   - off: 0.2609
   - on:  0.2598
4. balanced: env not set = complete non-regression - OK (verified by tests)

### Goal (target)
- kodim03_bytes(on) < off (target improvement) - **FAIL**
  - off: 369,747 bytes
  - on:  372,949 bytes (+3,202)

## Gate Verdict

**No-Go (as default strategy)**.  
Implementation is valid and test-clean, but compression regression is significant on fixed6 gate metrics.

## Observed Side Effect

LZCOST improved one image in this run (`kodim01`) but regressed multiple PHOTO-heavy samples:
- `kodim01`: 52,754 -> 52,754 (no change in 3-run median gate output used here)
- `kodim03`: 369,747 -> 372,949 (worse)
- `nature_01`: 812,576 -> 825,656 (worse)
- fixed6 total: +18,924 bytes (worse)

## Implementation Notes

### Two-Stage Selection Algorithm
1. **Stage 1**: Use BITS2 proxy to select top-K candidates (fast)
2. **Stage 2**: Evaluate top-K only with LZ cost estimation (more accurate)
3. **Tie-break**: Deterministic by filter ID order

### LZ Cost Estimation
- Input: uint8_t residuals (actual filter output)
- Window-based match detection (len >= 3)
- Cost model: literal=+1, match=+4 tokens
- Only enabled for PHOTO profile (configurable)

### Safety Features
- Opt-in via environment variable (default: disabled)
- PHOTO_ONLY flag to limit to photo profile only
- Falls back to BITS2 for non-PHOTO profiles when PHOTO_ONLY=1
- Verified bit-identical output when env unset

## Next Steps

1. Keep `lzcost` as opt-in experiment only (do not change default model).
2. Narrow evaluation to filtered row subsets (e.g., edge-heavy rows only) before full row replacement.
3. Revisit LZ proxy (current literal=1/match=4 is too coarse against real wrapper cost).
4. Consider block/plane-level gating before row-level override to avoid PHOTO-wide regressions.
