# 2026-02-14: mode2 nice-length high-range sweep (`128..255`) (No-Go)

## Objective
- Re-check whether `mode2 nice_length` can reduce chain-search cost while
  preserving strict balanced-lane size gate.

## Fixed Conditions
- `HAKONYANS_THREADS=1`
- `taskset -c 0`
- fixed6 (`kodim01`, `kodim02`, `kodim03`, `hd_01`, `nature_01`, `nature_02`)
- `--runs 3 --warmup 1`
- baseline:
  - `bench_results/phase9w_singlecore_lostream_lbcut_obs_20260214_runs3.csv`
- gate:
  - `total HKN bytes <= 2,977,544`
  - `median PNG/HKN >= 0.2610`

## Trial CSV
- `bench_results/phase9w_singlecore_mode2_nice128_vs_lostreamlb_20260214_runs3.csv`
- `bench_results/phase9w_singlecore_mode2_nice160_vs_lostreamlb_20260214_runs3.csv`
- `bench_results/phase9w_singlecore_mode2_nice192_vs_lostreamlb_20260214_runs3.csv`
- `bench_results/phase9w_singlecore_mode2_nice224_vs_lostreamlb_20260214_runs3.csv`
- `bench_results/phase9w_singlecore_mode2_nice255_vs_lostreamlb_20260214_runs3.csv`

## Aggregate Results (fixed6)
| nice_length | total HKN bytes | median PNG/HKN | median Enc(ms) | chain_steps (sum) | nice_hits (sum) | gate |
|---:|---:|---:|---:|---:|---:|---|
| 128 | 2,982,626 | 0.2610 | 175.353 | 29,077,809 | 10,952 | FAIL |
| 160 | 2,981,251 | 0.2610 | 173.219 | 29,146,907 | 7,764 | FAIL |
| 192 | 2,980,211 | 0.2610 | 173.024 | 29,192,294 | 5,700 | FAIL |
| 224 | 2,979,009 | 0.2610 | 173.093 | 29,224,838 | 4,169 | FAIL |
| 255 | 2,977,544 | 0.2610 | 172.720 | 29,242,731 | 0 | PASS |

## Interpretation
- Any `nice_length < 255` violated the size gate despite lower chain-step totals.
- With current `chain_depth=32`, cutoff tuning gives small speed upside and
  unacceptable size risk on fixed6.

## Decision
- No-Go for `128/160/192/224`.
- Keep `nice_length=255` as baseline-safe default.
- Keep telemetry/infrastructure for explicit lane-specific experiments only.
- Move next work to bit-identical mode2 inner-loop cost reduction instead of
  cutoff-value tuning.
