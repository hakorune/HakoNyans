# Phase 9p: Photo-Oriented Filter Lo Row Predictor

## 1. 目標

Phase 9o では UI/Anime は改善したが、Photo は `filter_lo` が支配的で改善が止まった。
このフェーズは Photo向けに `filter_lo` の予測変換を追加して圧縮率を伸ばす。

目標（lossless）:
- `nature_01` total を **-0.5%〜-3.0%**
- Photoカテゴリ平均で **-0.5%以上**
- UI/Anime は悪化しない（+1%以内）
- decode性能悪化を **+5%以内**

---

## 2. 背景（9o結果）

- `filter_lo` は UI/Anime で LZ が有効だった。
- Photo は mode0（legacy）選択が続き、`filter_lo` が大半を占めるまま。
- 次は「圧縮前変換」を足して、Photoで `filter_lo` の分布を鋭くする必要がある。

---

## 3. 実装タスク

## Task A: filter_lo mode3（Row Predictor）追加

対象ファイル:
- `src/codec/headers.h`
- `src/codec/encode.h`
- `src/codec/decode.h`

### A-1. バージョン/定数

`headers.h`:
- `VERSION` を `0x000B` に更新
- `VERSION_FILTER_LO_PRED = 0x000B` 追加
- `WRAPPER_MAGIC_FILTER_LO = 0xAB` は継続利用（mode拡張のみ）

### A-2. mode3 フォーマット

`filter_lo` wrapper:
- `[0xAB][mode=3][raw_count:u32][pred_stream_size:u32][pred_stream][resid_stream]`

定義:
- `pred_stream`: 各有効rowの predictor ID（0..3）を `encode_byte_stream` で圧縮
- `resid_stream`: predictor適用後の residual byte列を `encode_byte_stream` で圧縮

predictor ID:
- 0: NONE (`pred=0`)
- 1: SUB  (`pred=left`)
- 2: UP   (`pred=up`)
- 3: AVG  (`pred=(left+up)/2`)

※ `up` は「前の有効rowの同一列index」を参照。
   前行長が短い場合は `up=0` fallback。

### A-3. row segment の定義（重要）

`lo_bytes` は `filter_residuals` の走査順（row-major, filter blockのみ）なので、
エンコーダ/デコーダで同じ row segmentation を再構築する。

必要情報:
- `block_types`
- `pad_w/pad_h`

処理:
- 各画像row `y` について、その行の filter block pixel 数を `row_len[y]` として算出
- `row_len[y]==0` 行は predictor対象外
- `lo_bytes` 内で `row_len[y]` ずつ消費して rowごと処理

### A-4. rowごとの predictor 選択

各有効rowで 4候補を試し、最小コスト predictor を選択。

コスト関数（簡易）:
- `cost = sum(min(d, 256-d))`
- `d = (orig - pred) & 255`

選んだ predictor で residual を生成:
- `resid = (orig - pred) & 255`

### A-5. mode選択統合

既存の filter_lo mode0/1/2 に mode3 を追加し、最小サイズ選択:
- mode0: legacy
- mode1: delta+rANS
- mode2: LZ
- mode3: row predictor+rANS（新規）

採用条件:
- mode1/2/3 は `candidate < mode0 * 0.99`（1%以上改善）時のみ採用
- それ以外は mode0

---

## Task B: Photo-aware gate

対象ファイル:
- `src/codec/encode.h`

目的:
- UI/Anime で predictor mode が不要に選ばれるのを防ぐ

ゲート条件（初期値）:
- `is_photo_like` が true の tile で mode3 候補を有効
- non-photo tile は mode3 評価をスキップ（mode0/1/2 のみ）

`is_photo_like` は既存判定（`use_photo_mode_bias` 等）を流用してよい。

---

## Task C: テレメトリ追加

対象ファイル:
- `src/codec/encode.h`
- `bench/bench_bit_accounting.cpp`

追加カウンタ:
- `filter_lo_mode0/mode1/mode2/mode3`
- `filter_lo_mode3_rows_sum`
- `filter_lo_mode3_saved_bytes_sum`
- `filter_lo_mode3_pred_hist[4]`（NONE/SUB/UP/AVG）

出力:
- mode内訳
- mode3採用率
- predictorヒストグラム

---

## Task D: テスト追加

対象:
- `tests/test_lossless_round2.cpp`（または専用テスト）

最低3ケース:
1. mode3 roundtrip（row predictor）
2. mixed row_len（0長行を含む）roundtrip
3. malformed mode3 payload（pred/residサイズ不整合）で安全失敗

既存17テストは維持（PASS必須）。

---

## 4. 検証コマンド

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure

./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless
./build/bench_bit_accounting test_images/photo/nature_02.ppm --lossless
./build/bench_bit_accounting test_images/ui/vscode.ppm --lossless
./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless

cd build && ./bench_png_compare
./bench_decode
```

---

## 5. 受け入れ基準（DoD）

- `ctest` 17/17 PASS
- `bench_png_compare` 完走
- `nature_01` total: 9o基準（927573B）から **-0.5%以上**
- `nature_02` total: 9o基準から **-0.5%以上**
- `vscode` / `anime_girl_portrait` は悪化 **+1%以内**
- decode時間悪化が **+5%以内**

---

## 6. コミットメッセージ

```text
Phase 9p: add photo-oriented filter_lo row predictor mode
```

分割する場合:
1. `Phase 9p-1: add filter_lo mode3 row predictor wrapper`
2. `Phase 9p-2: add photo gate, telemetry, and validation`

---

## 7. 完了報告フォーマット

- 変更ファイル一覧
- mode3フォーマットと row segmentation ロジック
- predictor選択ロジック（コスト式）
- `ctest` 結果
- 4画像 before/after（nature_01, nature_02, vscode, anime）
- `bench_png_compare` カテゴリ差分
- decode時間差分
- 残課題（次フェーズ候補）
