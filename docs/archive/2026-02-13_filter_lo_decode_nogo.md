# 2026-02-13 filter_lo decode No-Go archive

## Objective

- `plane_filter_lo` の decode 速度改善を狙った実験のうち、
  mainline に採用しなかった試行を履歴として保存する。
- 圧縮率非劣化・フォーマット互換は全試行で維持。

## Baseline

- `bench_results/tmp_filterlo_cdf_fastpath_20260213_runs5.csv`
- Baseline median:
  - `hkn_enc_ms = 96.417`
  - `hkn_dec_ms = 8.010`
  - `hkn_dec_plane_filter_lo_ms = 5.628`
  - `hkn_dec_filter_lo_decode_rans_ms = 4.896`
  - `median PNG/HKN = 0.2610`
  - `total HKN bytes = 2,977,544`

## Trial A (No-Go): direct LUT build from serialized freq

### Scope

- `src/codec/decode.h`
  - LUT経路で CDF を介さず serialized freq から `SIMDDecodeTable` を直接構築。

### Artifacts

- `bench_results/tmp_filterlo_directlut_20260213_runs5.csv`
- `bench_results/tmp_filterlo_directlut_20260213_runs5_rerun.csv`

### A/B (median vs baseline)

| Run | Enc(ms) | Dec(ms) | filter_lo(ms) | decode_rans(ms) | PNG/HKN | total HKN bytes |
|---|---:|---:|---:|---:|---:|---:|
| run1 | -0.262 | +0.316 | +0.044 | +0.131 | 0.0000 | 0 |
| rerun | -0.953 | +0.139 | +0.045 | +0.214 | 0.0000 | 0 |

### Decision

- **No-Go (reverted)**  
  decode 側で悪化が再現し、`filter_lo` 内部（特に `decode_rans`）も改善しなかったため不採用。

## Trial B (No-Go): LUT switch threshold 128 -> 64

### Scope

- `src/codec/decode.h`
  - `decode_byte_stream` の LUT切替閾値を `count >= 128` から `count >= 64` に変更。

### Artifacts

- `bench_results/tmp_filterlo_lutmin64_20260213_runs5.csv`
- `bench_results/tmp_filterlo_lutmin64_20260213_runs5_rerun.csv`

### A/B (median vs baseline)

| Run | Enc(ms) | Dec(ms) | filter_lo(ms) | decode_rans(ms) | PNG/HKN | total HKN bytes |
|---|---:|---:|---:|---:|---:|---:|
| run1 | +2.844 | +0.261 | -0.077 | +0.129 | 0.0000 | 0 |
| rerun | +1.636 | +0.074 | -0.273 | -0.188 | 0.0000 | 0 |

### Decision

- **No-Go (reverted)**  
  stageの一部改善はあるが、wall（Enc/Dec）で安定改善を示せず、run1/rerun の揺れも大きいため不採用。

## Notes

- 両試行とも圧縮率とサイズは非劣化:
  - `median PNG/HKN = 0.2610` 不変
  - `total HKN bytes = 2,977,544` 不変
- いずれもコードは rebase/revert 済みで、履歴のみ本アーカイブに残す。
