# Phase 9o: Filter Lo Stream Delta Optimization

## 1. 目標

Phase 9n で `filter_ids` と `filter_hi` は改善できたため、次の主ボトルネックである
`filter_lo` を最適化する。

目標（lossless）:
- UI/Anime で total size を **-3%〜-8%**
- `filter_lo` bytes を **-5%〜-15%**
- Photo は悪化しない（**+1%以内**）
- decode性能悪化を **+5%以内**

---

## 2. 現状

- `filter_lo` は現状 `encode_byte_stream(lo_bytes)` 固定。
- 連続値の相関（局所差分）を活用できていない。
- mode選択がないため、画像特性に応じた最適化余地が残っている。

---

## 3. 実装タスク

## Task A: filter_lo wrapper 追加（legacy / delta / LZ）

対象ファイル:
- `src/codec/headers.h`
- `src/codec/encode.h`
- `src/codec/decode.h`

### A-1. ヘッダ拡張

`headers.h`:
- `VERSION` を `0x000A` に更新
- `VERSION_FILTER_LO_DELTA = 0x000A` 追加
- `WRAPPER_MAGIC_FILTER_LO = 0xAB` 追加

### A-2. lo wrapper 形式

`filter_lo` wrapper format:
- `[magic=0xAB][mode:u8][raw_count:u32][payload...]`

mode定義:
- `mode=0`: legacy raw byte stream（wrapperなし従来経路と同等）
- `mode=1`: delta変換 + rANS
- `mode=2`: raw bytes を TileLZ 圧縮

選択規則:
- 候補サイズを比較して最小を採用
- mode1/mode2 は `size < legacy * 0.99`（1%以上改善）時のみ採用
- 改善がなければ legacy を維持

### A-3. delta変換（mode1）

エンコード:
- `delta[0] = lo[0]`
- `delta[i] = uint8_t(lo[i] - lo[i-1])`（mod 256）
- `delta_enc = encode_byte_stream(delta)`

デコード:
- `lo[0] = delta[0]`
- `lo[i] = uint8_t(lo[i-1] + delta[i])`（mod 256）

---

## Task B: filter_lo RLE token（オプション強化）

対象ファイル:
- `src/codec/encode.h`
- `src/codec/decode.h`

mode=3 を追加する場合のみ実装:
- token: `[value:u8][run_minus1:u8]`（run=1..256）
- payload は token列
- 採用規則は mode1/mode2 と同じ（1%以上改善）

注意:
- まずは mode1+mode2 を必須実装し、mode3 は時間があれば追加。

---

## Task C: テレメトリ追加

対象ファイル:
- `src/codec/encode.h`
- `bench/bench_bit_accounting.cpp`

追加カウンタ:
- `filter_lo_mode0/mode1/mode2(/mode3)`
- `filter_lo_saved_bytes_sum`
- `filter_lo_delta_count`
- `filter_lo_lz_count`
- `filter_lo_rle_count`（mode3実装時）

表示追加:
- filter_lo mode内訳
- filter_lo raw vs compressed
- filter_lo 平均削減率

---

## Task D: テスト追加

対象:
- `tests/test_lossless_round2.cpp`（または専用テスト）

最低3ケース:
1. filter_lo mode1（delta）roundtrip
2. filter_lo mode2（LZ）roundtrip
3. malformed wrapper（壊れpayload）で安全失敗（クラッシュ禁止）

mode3実装時は追加:
4. filter_lo mode3（RLE）roundtrip

---

## 4. 検証コマンド

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure

./build/bench_bit_accounting test_images/ui/vscode.ppm --lossless
./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless
./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless

cd build && ./bench_png_compare
./bench_decode
```

---

## 5. 受け入れ基準（DoD）

- `ctest` 17/17 PASS
- `bench_png_compare` 完走
- `vscode` total: 9n基準（27829B）から **-3%以上**
- `anime_girl_portrait` total: 9n基準（12486B）から **-5%以上**
- `nature_01` total: 9n基準（927573B）に対して **+1%以内**
- decode時間悪化が **+5%以内**

---

## 6. コミットメッセージ

```text
Phase 9o: optimize filter_lo stream with delta wrapper and adaptive mode selection
```

分割例:
1. `Phase 9o-1: add filter_lo delta wrapper (legacy/delta/LZ)`
2. `Phase 9o-2: add filter_lo telemetry and validation`

---

## 7. 完了報告フォーマット

- 変更ファイル一覧
- filter_lo mode選択ロジック（legacy/delta/LZ/RLE）
- `ctest` 結果
- 3画像 before/after（filter_lo bytes と total）
- `bench_png_compare` カテゴリ差分
- decode時間差分
- 残課題（次フェーズ候補）
