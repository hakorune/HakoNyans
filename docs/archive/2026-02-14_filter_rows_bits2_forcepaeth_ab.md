# 2026-02-14 Filter Rows A/B (BITS2 / Force-Paeth)

## Scope
- Dataset: fixed 6 images
- Preset: `balanced`
- CPU mode: `HAKONYANS_THREADS=1` + `taskset -c 0`

## Variants
1. baseline (`HKN_FILTER_ROWS_COST_MODEL` unset, `HKN_FILTER_ROWS_FORCE_FILTER_ID` unset)
2. bits2 (`HKN_FILTER_ROWS_COST_MODEL=bits2`)
3. force-paeth (`HKN_FILTER_ROWS_FORCE_FILTER_ID=4`)
4. bits2+force-paeth

## Runs=3 Summary
- baseline: total `2,977,418`, median `PNG/HKN=0.261035`, Enc `209.039ms`, Dec `12.390ms`
- bits2: total `2,969,742`, median `PNG/HKN=0.261213`, Enc `206.325ms`, Dec `12.431ms`
- force-paeth: total `3,158,179`, median `PNG/HKN=0.262182`, Enc `173.160ms`, Dec `14.253ms`
- bits2+force-paeth: total `3,158,179`, median `PNG/HKN=0.262182`, Enc `174.336ms`, Dec `14.323ms`

## Runs=5 Recheck (baseline vs bits2)
- baseline: total `2,977,418`, median `PNG/HKN=0.261035`, Enc `200.521ms`, Dec `12.230ms`
- bits2: total `2,969,742`, median `PNG/HKN=0.261213`, Enc `207.583ms`, Dec `12.496ms`

Interpretation:
- `bits2` は圧縮率側で有効（total `-7,676 bytes`）だが、`runs=5` では Enc/Dec が悪化。
- strict non-regression gate（Enc/Dec）では `balanced` 既定への採用は保留。

## Per-image Size Delta
### runs=5 baseline vs bits2
- kodim01: `+0`
- kodim02: `-13`
- kodim03: `-146`
- hd_01: `-22,917`
- nature_01: `+18,217`
- nature_02: `-2,817`

### runs=3 baseline vs force-paeth
- kodim01: `-12`
- kodim02: `-76`
- kodim03: `-1,086`
- hd_01: `+65,092`
- nature_01: `+116,514`
- nature_02: `+329`

Interpretation:
- Force-Paeth は `kodim03` 単体では改善するが、全体では大幅悪化（no-go）。

## Filter Row Histogram (kodim03)
- baseline: `filter_row_hist(N/S/U/A/P/M) = 0/3/5/0/19/1509`
- bits2: `0/3/13/0/30/1490`
- force-paeth: `0/0/0/0/1536/0`

## Decision
1. `HKN_FILTER_ROWS_FORCE_FILTER_ID=4` は no-go（実験用維持のみ）。
2. `HKN_FILTER_ROWS_COST_MODEL=bits2` は size改善候補として保持。
   - `balanced` 既定へは未昇格（速度ゲート未達）
   - `max` / 圧縮優先レーンで継続評価が妥当。

## Artifacts
- `bench_results/phase9w_filterrows_ab_baseline_20260214_085542_runs3.csv`
- `bench_results/phase9w_filterrows_ab_bits2_20260214_085542_runs3.csv`
- `bench_results/phase9w_filterrows_ab_forcepaeth_20260214_085542_runs3.csv`
- `bench_results/phase9w_filterrows_ab_bits2_forcepaeth_20260214_085542_runs3.csv`
- `bench_results/phase9w_filterrows_ab_baseline_20260214_085542_runs5.csv`
- `bench_results/phase9w_filterrows_ab_bits2_20260214_085542_runs5.csv`
