# 2026-02-14 Filter Lo LZ Probe: Counter Integration and Sweep

## Scope
- Add probe telemetry so `filter_lo` LZ-skip behavior is observable in bench CSV and diagnostics.
- Sweep probe knobs on fixed6 (`preset=max`) and validate with rerun.

## Code Changes

### Telemetry fields
- `src/codec/lossless_mode_debug_stats.h`
  - Added:
    - `filter_lo_lz_probe_enabled`
    - `filter_lo_lz_probe_checked`
    - `filter_lo_lz_probe_pass`
    - `filter_lo_lz_probe_skip`
    - `filter_lo_lz_probe_sample_bytes_sum`
    - `filter_lo_lz_probe_sample_lz_bytes_sum`
    - `filter_lo_lz_probe_sample_wrapped_bytes_sum`

### Probe instrumentation
- `src/codec/lossless_filter_lo_codec.h`
  - `encode_filter_lo_stream(...)` now records:
    - probe enabled count
    - checked count
    - pass/skip count
    - sample raw/lz/wrapped byte sums

### Bench output wiring
- `bench/bench_png_compare.cpp`
  - Added CSV columns:
    - `hkn_enc_lo_lz_probe_enabled`
    - `hkn_enc_lo_lz_probe_checked`
    - `hkn_enc_lo_lz_probe_pass`
    - `hkn_enc_lo_lz_probe_skip`
    - `hkn_enc_lo_lz_probe_sample_bytes`
    - `hkn_enc_lo_lz_probe_sample_lz_bytes`
    - `hkn_enc_lo_lz_probe_sample_wrapped_bytes`
  - Added fixed6 summary line for probe counters and wrapped/raw ratio.

- `bench/bench_bit_accounting.cpp`
  - Added `Filter lo diagnostics` output for probe counters and sample ratio.

### Sweep automation
- `tools/sweep_filter_lo_lz_probe.py`
  - Sweeps:
    - `HKN_FILTER_LO_LZ_PROBE_THRESHOLD_PERMILLE`
    - `HKN_FILTER_LO_LZ_PROBE_SAMPLE_BYTES`
    - `HKN_FILTER_LO_LZ_PROBE_MIN_RAW_BYTES`
  - Uses fixed6 + `preset` target (`max` by default), single-core.
  - Outputs:
    - baseline CSV
    - per-combo CSVs
    - summary CSV + top10 text

## Validation
- Build: PASS
- Tests: `ctest` 17/17 PASS
- Smoke:
  - `balanced` fixed6: probe counters stay zero (probe off lane)
  - `max` fixed6: probe counters visible and non-zero

## Sweep Result (`preset=max`, runs=1, 36 combos)
- `total_hkn_bytes`: unchanged across all combos (`2,954,276`)
- `median PNG/HKN`: unchanged
- Gate (`size non-regression + ratio non-regression`): all PASS
- Probe behavior:
  - checked/skip totals unchanged across combos in this range

## Rerun Confirmation (`runs=3`)
- Candidate: `th=980, sample=4096, min_raw=2048`
- Baseline: defaults (`th=1030, sample=4096, min_raw=4096`)
- Result:
  - total bytes: equal
  - median encode: candidate slightly slower (~+1ms)
  - median decode: near-equal, slight noise-level increase

## Decision
- Keep default probe params as-is.
- Probe observability is now in place for later regressions.
- Next work should target core compression/selection logic (`mode2 eval`, filter-row quality/cost coupling), not probe threshold tuning.
