# Phase 9w: Natural最優先ルート（Global-chain LZ）実装指示書

## 目的
Naturalカテゴリ（`kodim*`, `hd_01`, `nature_*`）での PNG 比劣位を縮める。
特に「長距離反復を拾う能力」を強化し、Natural専用ルートを先に当てる。

## ゴール（DoD）
- `ctest --test-dir build --output-on-failure`: 17/17 PASS
- 固定6枚で A/B 比較を出力（baseline vs phase9w）
- 必須ログ出力:
  - `size_bytes`（HKN/PNG）
  - `Dec(ms)`
  - `natural_row_selected`（タイル採用率）
  - `gain_bytes` / `loss_bytes`（route競合内訳）
- Natural 6枚の中央値で `PNG_bytes / HKN_bytes` を改善（悪化禁止）

## 対象ファイル
- `src/codec/encode.h`
- `src/codec/lossless_natural_route.h`
- `src/codec/lossless_route_competition.h`
- `bench/bench_png_compare.cpp`
- `bench/bench_bit_accounting.cpp`
- （必要なら）`src/codec/headers.h`（フォーマット拡張時のみ）

## 実装タスク

### Task 1: Natural専用 global-chain LZ ルート追加
`lossless_natural_route` に新規ルートを追加:
- 既存の row-centric だけでなく、tile全体を連結した1本ストリームを候補化
- トークン列を `LITRUN + MATCH(len,dist)` 形式で生成
- `len>=4` 基本、`len==3 && dist<=64` は許可
- 探索は高速優先（hash + short chain）

最小実装方針:
- まず `filter_lo` 相当の高頻度ストリームを対象に global-chain LZ を適用
- 既存 route と `estimated_size` で競合
- 改善しない場合は採用しない（安全側）

### Task 2: 事前判定（screen-like / natural-like）
エンコード前に preflight 判定を固定:
- `screen-like`: 少色・run長が長い
- `natural-like`: 色数が多く、runが短い

判定は軽量サンプルベースで十分。
目的は「無駄な route 試行を減らす」こと。

推奨:
- `screen-indexed` を自然画像でむやみに試さない
- `natural-like` は global-chain LZ 候補を優先評価

### Task 3: 評価軸固定（6枚A/B）
毎回この6枚で同じ順番で測る:
- `test_images/photo/kodim01.ppm`
- `test_images/photo/kodim02.ppm`
- `test_images/photo/kodim03.ppm`
- `test_images/natural/hd_01.ppm`
- `test_images/photo/nature_01.ppm`
- `test_images/photo/nature_02.ppm`

必須出力:
- 画像ごとの `HKN bytes`, `PNG bytes`, `ratio`
- `Dec(ms)`
- `natural_row_selected` と新ルート採用率
- `gain_bytes`/`loss_bytes`

## 実行手順（順序固定）
1. build/test
```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

2. bit accounting（代表2枚）
```bash
./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless
./build/bench_bit_accounting test_images/natural/hd_01.ppm --lossless
```

3. PNG比較（全体）
```bash
./build/bench_png_compare
```

4. A/B 比較保存
- baseline と phase9w の結果を `bench_results/` に保存
- diff を残す

## 計測時の注意
- 速度比較は `DecSpeedup` ではなく `Dec(ms)` で統一
- 1回値だけでなく、最低3回の中央値を使う
- 失敗時はまず route採用ログを見る（採用されていない可能性が高い）

## 追記: 速度正面比較と段階観測（2026-02-12）

`bench_png_compare` に HKN/PNG の速度正面比較と HKN段階内訳を追加済み。

実行:
```bash
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --out bench_results/phase9w_speed_stage_profile.csv
```

必須確認:
- `median Enc(ms) HKN/PNG`
- `median Dec(ms) HKN/PNG`
- `HKN Stage Breakdown (median over fixed 6)`

現状の観測結果:
- Enc: `HKN/PNG = 3.132x`
- Dec: `HKN/PNG = 5.479x`
- Encode主ボトルネック:
  - `plane_route_comp`（route競合）
  - `plane_block_class`（block分類）
  - `plane_lo_stream`（filter_lo符号化）
- Decode主ボトルネック:
  - `plane_filter_lo`
  - `plane_reconstruct`
  - `plane_filter_hi`

## 並列化の現状メモ

進んでいる箇所:
- 主に lossy decode 側（Cb/Cr の `std::async`、AC band並列、block並列）

未着手/不足している箇所:
- lossless encode の `encode_plane_lossless` 本体
- lossless route 競合 (`route_compete`)
- lossless decode の `lossless_plane_decode_core`（`filter_lo`/再構築）

次フェーズでは、まず lossless 経路の並列化を優先すること。

## 失敗パターンと対処
- LZ後にサイズ膨張:
  - 小ストリームでは CDF/ラッパ固定費が支配
  - `min_raw_bytes` / `min_lz_bytes` gate を上げる
- Naturalで改善しない:
  - chain長と match条件を再調整
  - preflightで誤分類していないか確認
- Decode劣化:
  - route採用条件を厳格化して fallback 率を上げる

## コミットメッセージ例
`Phase 9w: add natural global-chain LZ route with fixed pre-classification and 6-image A/B telemetry`
