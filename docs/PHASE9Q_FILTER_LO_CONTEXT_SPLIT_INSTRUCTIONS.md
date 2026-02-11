# Phase 9q: Filter Lo Context-Split Optimization

## 1. 目標

Phase 9p では row predictor mode を実装したが、Photoで mode0 が選ばれ改善が出なかった。
このフェーズは `filter_lo` を `filter_id` 文脈で分割して符号化し、Photoの圧縮率改善を狙う。

目標（lossless）:
- `nature_01` total を **-0.5%〜-2.5%**
- `nature_02` total を **-0.5%〜-2.5%**
- UI/Anime は悪化しない（+1%以内）
- decode性能悪化を **+5%以内**

---

## 2. 現状課題

- `filter_lo` は Photoで支配的（totalの大半）
- 単一streamでは filter種別ごとの分布差を活かせない
- 9p mode3 は機能OKだが、サイズ面で mode0 に勝てなかった

---

## 3. 実装タスク

## Task A: filter_lo mode4（Context Split）追加

対象ファイル:
- `src/codec/headers.h`
- `src/codec/encode.h`
- `src/codec/decode.h`

### A-1. バージョン定数

`headers.h`:
- `VERSION` を `0x000C` に更新
- `VERSION_FILTER_LO_CONTEXT_SPLIT = 0x000C` を追加
- `WRAPPER_MAGIC_FILTER_LO = 0xAB` は継続（mode拡張）

### A-2. mode4 フォーマット

`filter_lo` wrapper (mode=4):
- `[0xAB][mode=4][raw_count:u32]`
- `[len0:u32][len1:u32][len2:u32][len3:u32][len4:u32][len5:u32]`
- `[stream0][stream1][stream2][stream3][stream4][stream5]`

ここで `streamK = encode_byte_stream(lo_ctx[K])`。
`lo_ctx[K]` は filter_id==K の画素だけを走査順で抜き出した byte列。

### A-3. context分割ロジック

`filter_ids[y]` が row predictor type を持つので、`filter_residuals` と同じ走査順で:
- 画素 `(x,y)` が filter block のとき
- `fid = filter_ids[y]`（0..5）
- `lo_byte` を `lo_ctx[fid]` へ push

デコード時は同じ走査順で:
- `fid` に対応する context stream から1byte pop
- 元の `lo_bytes` 配列へ復元

### A-4. mode選択統合

既存 mode0/1/2/3 に mode4 を追加して最小サイズ選択:
- mode0: legacy
- mode1: delta+rANS
- mode2: LZ
- mode3: row predictor
- mode4: context split（新規）

採用条件:
- mode1/2/3/4 は `candidate < mode0 * 0.99`（1%以上改善）で採用
- 改善がなければ mode0維持

---

## Task B: Photo-only gate + fallback

対象ファイル:
- `src/codec/encode.h`

方針:
- mode4 は `is_photo_like` tile でのみ候補に入れる
- non-photo tile は mode4をスキップ
- 常に mode0 fallback を保持（回帰防止）

---

## Task C: テレメトリ追加

対象ファイル:
- `src/codec/encode.h`
- `bench/bench_bit_accounting.cpp`

追加カウンタ:
- `filter_lo_mode4`
- `filter_lo_mode4_saved_bytes_sum`
- `filter_lo_ctx_bytes_sum[6]`
- `filter_lo_ctx_nonempty_tiles`

出力:
- mode0/1/2/3/4 内訳
- mode4採用率
- context別生bytes/圧縮bytes（可能なら）

---

## Task D: テスト追加

対象:
- `tests/test_lossless_round2.cpp`（または専用テスト）

最低3ケース:
1. mode4 roundtrip（全context使用）
2. mode4 roundtrip（一部context空）
3. malformed mode4 payload（len不整合/短いstream）で安全失敗

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
- `nature_01` total: 9o/9p基準（927573B）から **-0.5%以上**
- `nature_02` total: 9o/9p基準から **-0.5%以上**
- `vscode` / `anime_girl_portrait` は悪化 **+1%以内**
- decode時間悪化が **+5%以内**

---

## 6. コミットメッセージ

```text
Phase 9q: add filter_lo context-split mode for photo tiles
```

分割例:
1. `Phase 9q-1: add filter_lo mode4 context-split wrapper`
2. `Phase 9q-2: add photo gate, telemetry, and validation`

---

## 7. 完了報告フォーマット

- 変更ファイル一覧
- mode4フォーマットと context split ロジック
- mode選択ロジック（0/1/2/3/4）
- `ctest` 結果
- 4画像 before/after（nature_01, nature_02, vscode, anime）
- `bench_png_compare` カテゴリ差分
- decode時間差分
- 残課題（次フェーズ候補）
