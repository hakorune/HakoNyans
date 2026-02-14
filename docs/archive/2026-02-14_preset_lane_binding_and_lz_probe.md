# 2026-02-14 Preset Lane Binding + LZ Probe

## Scope
Implemented two structural changes:
1. Preset-level policy binding for filter-row cost model.
2. filter_lo LZ probe to skip expensive mode2/mode5 evaluation when likely unhelpful.

Also added source-doc linkage map:
- `docs/LOSSLESS_FLOW_MAP.md`

## Code Changes
- `src/codec/encode.h`
  - `LosslessPresetPlan` extended with:
    - `filter_row_cost_model`
    - `filter_lo_lz_probe_enable`
  - preset defaults:
    - `fast`: SAD + probe on
    - `balanced`: SAD + probe off
    - `max`: ENTROPY + probe on
  - env toggles:
    - `HKN_FAST_FILTER_LO_LZ_PROBE`
    - `HKN_BALANCED_FILTER_LO_LZ_PROBE`
    - `HKN_MAX_FILTER_LO_LZ_PROBE`
  - `encode_plane_lossless(...)` now receives these preset controls.

- `src/codec/lossless_filter_rows.h`
  - Added env-aware resolver with preset default fallback.
  - `build_filter_rows_and_residuals(...)` now accepts preset default model.

- `src/codec/lossless_filter_lo_codec.h`
  - Added LZ probe runtime params and threshold parsing:
    - `HKN_FILTER_LO_LZ_PROBE_MIN_RAW_BYTES`
    - `HKN_FILTER_LO_LZ_PROBE_SAMPLE_BYTES`
    - `HKN_FILTER_LO_LZ_PROBE_THRESHOLD`
    - `HKN_FILTER_LO_LZ_PROBE_THRESHOLD_PERMILLE`
  - Probe method: sample `TileLZ` compression ratio check before full mode2/mode5 evaluation.

## Validation
- Build + test:
  - `cmake --build build -j8`
  - `ctest --test-dir build --output-on-failure`
  - Result: 17/17 PASS

## Quick Preset Checks (fixed6, runs=1)
- balanced:
  - `bench_results/tmp_preset_balanced_after_lanebind_20260214.csv`
  - total: `2,977,418`
  - median PNG/HKN: `0.2610`

- max:
  - `bench_results/tmp_preset_max_after_lanebind_20260214.csv`
  - total: `2,954,276`
  - median PNG/HKN: `0.2609`

- balanced + env entropy override:
  - `bench_results/tmp_preset_balanced_enventropy_after_lanebind_20260214.csv`
  - total: `2,954,069`
  - median PNG/HKN: `0.2609`

Interpretation:
- Preset binding works (`balanced` keeps SAD baseline behavior).
- `max` picks compression-heavier policy.
- Env override still works for experimentation.
