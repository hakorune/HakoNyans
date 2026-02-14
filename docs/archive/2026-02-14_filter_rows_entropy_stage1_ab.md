# 2026-02-14 Filter Rows Entropy Stage1 A/B

## 変更
- `src/codec/lossless_filter_rows.h`
  - `HKN_FILTER_ROWS_COST_MODEL=entropy` を本格化
  - 旧: `zz & 0xFF` 加算の簡易近似
  - 新: ヒストグラム + Shannon entropy 推定（lo/hi 別）
  - 2段評価導入:
    1. BITS2 近似で候補ランキング
    2. 上位 `top-k` だけ entropy 精査
- 追加 env:
  - `HKN_FILTER_ROWS_ENTROPY_TOPK` (default: 2)
  - `HKN_FILTER_ROWS_ENTROPY_HI_WEIGHT_PERMILLE` (default: 350)

## テスト
- `cmake --build build -j8`
- `ctest --test-dir build --output-on-failure`
- 結果: 17/17 PASS
- 追加テスト:
  - `test_filter_rows_entropy_differs_from_sad`
  - `test_filter_rows_entropy_env_roundtrip`

## kodim03 単体観測
- SAD:
  - total: `369,844`
  - row_hist(N/S/U/A/P/M): `0/3/5/0/19/1509`
- entropy(stage1):
  - total: `369,747` (`-97`)
  - row_hist(N/S/U/A/P/M): `0/3/13/0/24/1496`

## fixed6 A/B (`balanced`, single-core, runs=3)
- baseline (SAD):
  - CSV: `bench_results/phase9w_filterrows_entropy_stage1_sad_20260214_runs3.csv`
  - total: `2,977,418`
  - median PNG/HKN: `0.2610`
  - median Enc: `207.242 ms`
  - median Dec: `13.254 ms`
- entropy(stage1):
  - CSV: `bench_results/phase9w_filterrows_entropy_stage1_20260214_runs3.csv`
  - total: `2,954,069` (`-23,349`)
  - median PNG/HKN: `0.2609` (微減)
  - median Enc: `223.846 ms` (`+16.604 ms`)
  - median Dec: `13.193 ms` (`-0.061 ms`)

## 判定
- 圧縮量（total bytes）は大幅改善。
- ただし `balanced` gate の `median PNG/HKN >= 0.2610` に僅差で届かない。
- `balanced` 既定への昇格は保留。`max` / 圧縮優先レーンでの運用候補。
