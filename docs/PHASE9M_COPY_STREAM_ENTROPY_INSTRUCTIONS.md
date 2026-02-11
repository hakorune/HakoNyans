# Phase 9m: Copy Stream Entropy/RLE Optimization

## 1. 目標

Phase 9l で `block_types` は大きく縮小したが、UIでは `copy stream` が依然として最大コスト。
このフェーズは `copy stream` 専用に圧縮率を改善する。

目標（lossless）:
- UI系で `copy stream` bytes を **-10%〜-25%**
- Animeで `copy stream` bytes を **-5%〜-15%**
- Photoで悪化しない（±1%以内）

---

## 2. 現状前提

現行 `CopyCodec` モード:
- mode=0: raw dx/dy (4 bytes/op)
- mode=1: 固定2-bit small-vector index
- mode=2: dynamic small-vector codebook（used_mask + N-bit）

観測:
- `vscode` で `copy_stream_bytes` が依然大きい
- mode2 が選ばれても 1bit/op 付近で頭打ち

---

## 3. 実装タスク

## Task A: CopyCodec Mode3（Run-Length Token）

ファイル: `src/codec/copy.h`

`use_small == true` の場合に mode3 候補を追加。

### Mode3 フォーマット（固定）
- header: `[mode=3][used_mask]`
- payload: run token列
- run token 1byte:
  - bit7..6: `symbol_code` (0..3)
  - bit5..0: `run_minus1` (0..63)
- run長 = `run_minus1 + 1`（1..64）
- runが64超なら複数tokenに分割

### エンコード
1. mode2と同じ `used_mask` と `small_to_code` を作る
2. symbol列を走査して run-length 化
3. `mode3_size = 2 + run_tokens` を算出
4. mode1/mode2/mode3 の最小サイズを選択

### デコード
- mode=3 を追加
- `used_mask` から `code_to_small` を復元
- tokenを順に展開して `num_blocks` 分の `CopyParams` を出力
- 異常tokenや過不足は fail-safe で mode0相当の安全終了（クラッシュ禁止）

---

## Task B: テレメトリ追加

ファイル:
- `src/codec/encode.h`
- `bench/bench_bit_accounting.cpp`

追加カウンタ:
- `copy_stream_mode3`
- `copy_mode3_run_tokens_sum`
- `copy_mode3_runs_sum`
- `copy_mode3_long_runs`（run>=16）

表示追加:
- mode0/1/2/3 の内訳
- mode3 の平均run長
- mode3採用率

---

## Task C: 互換性

ファイル: `src/codec/headers.h`

- `VERSION` を `0x0008` に更新
- `VERSION_COPY_MODE3 = 0x0008` を追加

デコード側ポリシー:
- 新デコーダは mode0/1/2/3 全対応
- 旧ファイルは従来通り読める

---

## Task D: テスト追加

ファイル: `tests/test_lossless_round2.cpp`（または専用test追加）

最低3ケース:
1. mode3 roundtrip（長run多数）
2. mode3 mixed runs（短run/長run混在）
3. malformed mode3 payload（安全に失敗・クラッシュなし）

---

## 4. 検証コマンド

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure

./build/bench_bit_accounting test_images/ui/vscode.ppm --lossless
./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless
./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless

# 必ず build ディレクトリから
cd build && ./bench_png_compare
./bench_decode
```

---

## 5. 受け入れ基準（DoD）

- `ctest` 17/17 PASS
- `bench_png_compare` 完走
- `vscode` の `copy_stream_bytes` が baseline 比で **-10%以上**
- `anime_girl_portrait` の `copy_stream_bytes` が baseline 比で **-5%以上**
- `nature_01` の total size 悪化が **+1%以内**
- decode時間悪化が **+5%以内**

---

## 6. コミットメッセージ

```text
Phase 9m: add copy stream mode3 RLE entropy coding
```

必要なら分割:
1. `Phase 9m-1: add mode3 run-length coding in CopyCodec`
2. `Phase 9m-2: add copy mode3 telemetry and validation`

---

## 7. 完了報告フォーマット

- 変更ファイル一覧
- モード選択ロジック（mode1/2/3）の要点
- `ctest` 結果
- `bench_bit_accounting` 3画像比較（before/after）
- `bench_png_compare` カテゴリ差分
- decode時間差分
- 残課題
