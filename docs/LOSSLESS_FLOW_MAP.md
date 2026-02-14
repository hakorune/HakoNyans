# Lossless Flow Map

This file links lossless implementation points to the design intent, so source and docs stay connected.

## Preset Policy
Source:
- `src/codec/encode.h` (`LosslessPresetPlan`, `build_lossless_preset_plan`)

Rules:
- `fast`: speed-first tuning, conservative compression work
- `balanced`: non-regression default lane
- `max`: compression-first experiments and heavier heuristics

Current mapping:
- `fast`: `filter_row_cost_model = SAD`, `filter_lo_lz_probe = on`
- `balanced`: `filter_row_cost_model = SAD`, `filter_lo_lz_probe = off`
- `max`: `filter_row_cost_model = ENTROPY`, `filter_lo_lz_probe = on`

Env overrides:
- `HKN_FILTER_ROWS_COST_MODEL` (experiment override)
- `HKN_FAST_FILTER_LO_LZ_PROBE`
- `HKN_BALANCED_FILTER_LO_LZ_PROBE`
- `HKN_MAX_FILTER_LO_LZ_PROBE`

## Filter Row Selection
Source:
- `src/codec/lossless_filter_rows.h` (`build_filter_rows_and_residuals`)

Pipeline:
1. Build candidate filters per row.
2. Cost model chosen by preset default, optionally overridden by env.
3. Cost models:
- `sad`: absolute residual sum
- `bits2`: LUT-based symbol-bit proxy
- `entropy`: stage-1 two-step evaluation

Entropy stage-1 method:
1. Coarse rank by `bits2`.
2. Refine top-k candidates with Shannon entropy over residual `lo/hi` histograms.

Entropy knobs:
- `HKN_FILTER_ROWS_ENTROPY_TOPK`
- `HKN_FILTER_ROWS_ENTROPY_HI_WEIGHT_PERMILLE`

## Filter Lo LZ Probe
Source:
- `src/codec/lossless_filter_lo_codec.h` (`encode_filter_lo_stream`)

Purpose:
- Skip expensive mode2/mode5 LZ evaluation when a small sample predicts poor LZ benefit.

Method:
1. Sample first N bytes of `lo_bytes`.
2. Run `TileLZ::compress` on sample.
3. If sample wrapped ratio exceeds threshold, skip full LZ path.

Probe knobs:
- `HKN_FILTER_LO_LZ_PROBE_MIN_RAW_BYTES`
- `HKN_FILTER_LO_LZ_PROBE_SAMPLE_BYTES`
- `HKN_FILTER_LO_LZ_PROBE_THRESHOLD` (float)
- `HKN_FILTER_LO_LZ_PROBE_THRESHOLD_PERMILLE` (int override)

Telemetry:
- Encode stats (`src/codec/lossless_mode_debug_stats.h`):
  - `filter_lo_lz_probe_enabled`
  - `filter_lo_lz_probe_checked`
  - `filter_lo_lz_probe_pass`
  - `filter_lo_lz_probe_skip`
  - `filter_lo_lz_probe_sample_bytes_sum`
  - `filter_lo_lz_probe_sample_lz_bytes_sum`
  - `filter_lo_lz_probe_sample_wrapped_bytes_sum`
- CSV (`bench/bench_png_compare.cpp`):
  - `hkn_enc_lo_lz_probe_*`

## Usage Rule
When changing behavior in these source files, update this map in the same commit.
