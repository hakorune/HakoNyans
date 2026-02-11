# Phase 9l LZ Debug Instructions (Gemini向け)

## 0. 問題概要

Phase 9l（tile-local LZ）導入後、以下の停止症状が出る。

- `./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless` がタイムアウト
- `./build/bench_png_compare` が `UI/browser` で停止
- ただし `ctest` は `17/17 PASS`

このため「テストは通るが実運用ベンチでハング/異常に遅い」状態。

---

## 1. 最優先仮説（P0）

## H1: block_types Mode1 の実装が不正

`encode_block_types()` 内で Mode1 生成時に `encode_tokens(build_cdf(...))` を使っているが、
`build_cdf()` は alphabet=76 前提。

一方 `block_types` の raw run byte は `0..255` を取りうる。
この不整合で未定義動作（範囲外シンボル）が発生し、ハング/異常遅延の原因になる可能性が高い。

### 修正方針（必須）

- `block_types Mode1` は `encode_byte_stream(raw)` を使う（256-alphabet）
- `decode_block_types()` の Mode1 も `decode_byte_stream(...)` で対称にする
- `encode_tokens/build_cdf(76)` を block_types Mode1 で使わない

---

## 2. 二次仮説（P1）

## H2: TileLZ の最悪ケースで探索が重すぎる

`TileLZ::compress()` が hash単一エントリ＋逐次延長で、特定データで非常に重くなる可能性。

### 修正方針（必要なら）

- チェーン長上限（例: 8）を導入
- lazy match を無効化（最初は greedy固定）
- `len==3` は `dist<=64` の時のみ許可
- `src_size` が小さい場合は早期に raw返却

---

## 3. 三次仮説（P1）

## H3: wrapper decode のフォールバックが不十分

`mode=2` 失敗時に中途半端なデータを使って decode 継続している箇所があると、
後段で異常ループを誘発する可能性。

### 修正方針（必須）

- `decompress_to(...)` で `raw_count` ぴったり出た時のみ採用
- 失敗時は legacy/raw のみに戻す
- invalid wrapper は fail-safe（そのstreamだけ raw path）

---

## 4. 再現手順（固定）

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure

# ハング再現（20秒で打ち切り）
timeout 20s ./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless; echo EXIT:$?
timeout 20s ./build/bench_png_compare; echo EXIT:$?
```

期待:
- 現状は `EXIT:124`（timeout）が再現するはず

---

## 5. 切り分け手順（順序固定）

1. **H1修正を先に適用**（block_types Mode1 を byte-stream化）
2. 再ビルド・再実行して timeout 解消を確認
3. まだ止まる場合のみ H2（LZ探索上限）を入れる
4. それでも止まる場合 H3（wrapper fail-safe）を強化

---

## 6. 実装対象ファイル

- `src/codec/encode.h`
- `src/codec/decode.h`
- `src/codec/lz_tile.h`（必要時のみ）
- `tests/test_lossless_round2.cpp`
- `bench/bench_bit_accounting.cpp`（必要ならログ追加）

---

## 7. 必須テスト追加

## T1: block_types mode1 roundtrip

- `0..255` を含む run-byte 列を encode/decode して一致を検証

## T2: LZ wrapper malformed input

- 壊れた `mode=2` payload で decode がクラッシュしないこと
- `raw_count` 不一致時に fail-safe へ戻ること

## T3: anime timeout regression

- `anime_girl_portrait` を対象に `bench_bit_accounting` が 10秒以内完走

---

## 8. 受け入れ基準（DoD）

- `ctest` 17/17 PASS
- `timeout 20s ./build/bench_bit_accounting ...anime...` が timeoutしない
- `timeout 20s ./build/bench_png_compare` が完走
- UI系で block_types LZ効果を維持（vscodeで `block_types` が明確に減る）
- Photoで悪化しない（`nature_01` 大幅悪化なし）

---

## 9. 期待アウトプット（報告フォーマット）

- 根本原因（H1/H2/H3のどれか）
- 変更ファイル一覧
- 修正要点
- テスト結果
- ベンチ結果（vscode/anime/nature_01）
- `bench_png_compare` の完走有無
- 残課題

---

## 10. コミットメッセージ例

```text
Phase 9l-debug: fix block_types mode1 symbol-range bug and timeout regression
```

補助コミットが必要なら:

```text
Phase 9l-debug: harden tile-local LZ decode fail-safe path
```
