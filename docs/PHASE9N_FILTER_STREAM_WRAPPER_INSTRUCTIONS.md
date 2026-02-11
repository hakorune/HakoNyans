# Phase 9n: Filter Stream Wrapper Optimization

## 1. 目標

filter_ids（raw 1B/row）と filter_hi（rANS 高バイト）は UI/Anime で大きな割合を占める。
このフェーズではこの2ストリームを圧縮し、全体サイズを削減する。

目標（lossless）:
- UI系で **total size -3〜-10%**
- Animeで **total size -3〜-10%**
- Photoで悪化しない（**+1%以内**）
- decode速度悪化 **+5%以内**

---

## 2. 現状前提

### ベースライン

| Image | filter_ids | filter_hi | total | filter_ids% | filter_hi% |
|-------|-----------|----------|-------|-------------|------------|
| vscode (UI) | 3240 | 3708 | 30790 | 10.5% | 12.0% |
| anime_girl | 3240 | 3485 | 15679 | 20.7% | 22.2% |
| nature_01 | 3240 | 34482 | 927896 | 0.35% | 3.7% |

観測:
- `filter_ids` は常に raw 1B/row。3 plane × 1080 rows = 3240B で固定
- フィルタ種別は 0〜5 の 6 種のみ（3bit で十分）
- `filter_hi` は ZigZag high byte。UI/Anime では大半が 0（残差が 8bit に収まる）
- `filter_hi` は既に rANS エンコード済みだが、ゼロ支配のため sparse 表現が有効

### タイルデータレイアウト（32B header）

```
[4B filter_ids_size][4B lo_stream_size][4B hi_stream_size][4B filter_pixel_count]
[4B bt_data_size][4B pal_data_size][4B cpy_data_size][4B tile4_data_size]
[filter_ids][lo_stream][hi_stream][block_types][palette][copy][tile4]
```

### フィルタ種別（LosslessFilter::FilterType）

| ID | Name | Description |
|----|------|-------------|
| 0 | None | Identity |
| 1 | Sub | Left prediction |
| 2 | Up | Above prediction |
| 3 | Average | (Left+Above)/2 |
| 4 | Paeth | PNG Paeth predictor |
| 5 | MED | JPEG-LS MED (photo only) |

### 既存コード参照

- `src/codec/lossless_filter.h` — FilterType enum, filter_image(), unfilter_image()
- `src/codec/encode.h` line ~1527–1610 — filter_ids 生成、lo/hi split、rANS encode
- `src/codec/encode.h` line ~1717–1738 — tile data packing (32B header)
- `src/codec/decode.h` line ~656–685 — tile data unpacking、filter_ids 読み込み
- `src/codec/decode.h` line ~837 — filter_ids[y] 参照
- `src/codec/lz_tile.h` — TileLZ::compress / decompress
- `src/codec/encode.h` `encode_byte_stream()` — rANS byte stream encoder
- `src/codec/decode.h` `decode_byte_stream()` — rANS byte stream decoder

---

## 3. 実装タスク

### Task A: filter_ids 圧縮（raw / rANS / LZ 最小選択）

ファイル: `src/codec/encode.h`, `src/codec/decode.h`

#### エンコード

1. 現行: `filter_ids` (std::vector<uint8_t>, height entries) を raw でそのまま格納
2. 新方式: 3 候補を試して最小サイズを選択:
   - **Mode 0 (raw)**: そのまま（現行互換）
   - **Mode 1 (rANS)**: `encode_byte_stream(filter_ids)` で圧縮
   - **Mode 2 (LZ)**: `TileLZ::compress(filter_ids.data(), filter_ids.size())` で圧縮
3. Mode 0 が最小なら raw のまま格納（後方互換を維持）
4. Mode 1/2 が最小なら wrapper で包む:
   - `[0xA9][mode][compressed data]`
   - mode = 1 (rANS), 2 (LZ)
5. wrapper magic: `0xA9` (`WRAPPER_MAGIC_FILTER_IDS`)

#### デコード

1. `filter_ids_size` の先頭バイトが `0xA9` なら wrapper として解釈:
   - pos=0 → magic (0xA9)
   - pos=1 → mode (1 or 2)
   - pos=2.. → compressed data
2. mode=1: `decode_byte_stream(data+2, size-2, height)` で rANS デコード
3. mode=2: `TileLZ::decompress(data+2, size-2)` で LZ デコード
4. magic が `0xA9` でなければ raw として読む（後方互換）
5. 異常時は全行 filter=0 (None) で安全終了（クラッシュ禁止）

#### 注意事項
- filter_ids のアルファベットは 0..5 のみだが、rANS は 256 シンボル対応の `encode_byte_stream` を使えばよい
- filter_ids は 1 tile あたり height (最大 1080) エントリ程度。小さいデータなので overhead に注意

---

### Task B: filter_hi sparse モード

ファイル: `src/codec/encode.h`, `src/codec/decode.h`

#### 概要

UI/Anime の filter_hi は 90%以上がゼロ。sparse 表現で大幅圧縮可能。

#### Sparse フォーマット

```
[0xAA]                  // magic (1 byte)
[nonzero_count_lo]      // nonzero count lower 8 bits
[nonzero_count_hi_lo]   // nonzero count bits 15..8
[nonzero_count_hi_hi]   // nonzero count bits 23..16
[zero_mask]             // ceil(pixel_count / 8) bytes, bit=1 means nonzero
[nonzero_hi_rANS]       // rANS encoded nonzero high bytes only
```

- `0xAA` = `WRAPPER_MAGIC_FILTER_HI_SPARSE`
- nonzero_count は 24bit（最大 16M ピクセル対応）

#### エンコード（encode.h の Step 3 を拡張）

1. hi_bytes のゼロ比率を計算: `zero_count / total_count`
2. ゼロ比率 >= 75% の場合に sparse 候補を生成:
   a. zero_mask を生成: `ceil(pixel_count / 8)` bytes, bit=1 for nonzero
   b. nonzero 値のみ抽出して `encode_byte_stream()` で rANS 圧縮
   c. sparse_size = 4 + mask_size + nonzero_rans_size
3. 通常 rANS サイズ (`encode_byte_stream(hi_bytes).size()`) と sparse サイズを比較
4. 小さい方を選択

#### デコード（decode.h を拡張）

1. hi_stream 先頭が `0xAA` なら sparse モード:
   a. nonzero_count を読む (3 bytes, little endian)
   b. mask_size = ceil(filter_pixel_count / 8) を計算
   c. zero_mask を読む
   d. nonzero hi values を `decode_byte_stream()` で rANS デコード
   e. mask に基づいて hi_bytes を復元:
      - mask bit=1 → 次の nonzero value を使用
      - mask bit=0 → 0
2. 先頭が `0xAA` でなければ従来の rANS デコード（後方互換）
3. 異常時は全 hi=0 で安全終了（残差の high byte が 0 なら low byte のみで近似復元）

---

### Task C: 互換性

ファイル: `src/codec/headers.h`

- `VERSION` を `0x0009` に更新
- `VERSION_FILTER_WRAPPER = 0x0009` を追加
- `WRAPPER_MAGIC_FILTER_IDS = 0xA9` を追加
- `WRAPPER_MAGIC_FILTER_HI_SPARSE = 0xAA` を追加

デコード側ポリシー:
- 新デコーダは raw + wrapped 両方を自動判定（magic byte で分岐）
- 旧ファイルは従来通り読める（magic が 0xA9/0xAA でなければ raw/通常 rANS）

---

### Task D: テレメトリ追加

ファイル: `src/codec/encode.h`, `bench/bench_bit_accounting.cpp`

追加カウンタ（`LosslessModeDebugStats`）:
- `filter_ids_mode0` / `filter_ids_mode1` / `filter_ids_mode2` (raw/rANS/LZ 選択回数)
- `filter_ids_raw_bytes_sum` (圧縮前サイズ累計)
- `filter_ids_compressed_bytes_sum` (圧縮後サイズ累計)
- `filter_hi_sparse_count` (sparse モード選択回数)
- `filter_hi_dense_count` (従来 rANS 選択回数)
- `filter_hi_zero_ratio_sum` (ゼロ比率累計、/count で平均算出)
- `filter_hi_raw_bytes_sum` (圧縮前サイズ累計)
- `filter_hi_compressed_bytes_sum` (圧縮後サイズ累計)

表示追加（`bench_bit_accounting.cpp`）:
```
  Filter stream diagnostics
  filter_ids_mode0/1/2     X/Y/Z
  filter_ids_bytes         raw=XXXXX compressed=YYYYY (ZZ.Z% savings)
  filter_hi_sparse/dense   X/Y
  filter_hi_avg_zero_ratio ZZ.Z%
  filter_hi_bytes          raw=XXXXX compressed=YYYYY (ZZ.Z% savings)
```

---

### Task E: テスト追加

ファイル: `tests/test_lossless_round2.cpp`（既存 test に追加）

最低 4 ケース:

1. **filter_ids rANS roundtrip**
   - 全行同一フィルタ（例: 全部 FILTER_SUB=1）の画像を encode → decode
   - ビット完全一致を検証

2. **filter_ids LZ roundtrip**
   - 周期的フィルタパターン（例: [0,1,0,1,...] 繰り返し）の画像を encode → decode
   - LZ が選択されることを確認（optional, 選択されなくてもOK）
   - ビット完全一致を検証

3. **filter_hi sparse roundtrip**
   - 残差の絶対値が 255 以下の画像（hi = 全 0）を encode → decode
   - ビット完全一致を検証

4. **malformed wrapper**
   - 不正な magic/mode の filter_ids データを decode に渡す → クラッシュなし
   - 不正な sparse magic の hi_stream データを decode に渡す → クラッシュなし

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

- `ctest` 全 PASS（既存テスト + 新規テスト）
- `bench_png_compare` 完走
- `vscode` の total size が baseline (30790) 比 **-3%以上** (≤ 29866)
- `anime_girl` の total size が baseline (15679) 比 **-3%以上** (≤ 15208)
- `nature_01` の total size 悪化が **+1%以内** (≤ 937175)
- decode時間悪化が **+5%以内**

---

## 6. コミットメッセージ

```text
Phase 9n: add filter stream wrapper (filter_ids compression + filter_hi sparse mode)
```

---

## 7. 完了報告フォーマット

- 変更ファイル一覧
- filter_ids モード選択ロジック（raw/rANS/LZ）の要点
- filter_hi sparse モード判定ロジックの要点
- `ctest` 結果
- `bench_bit_accounting` 3画像比較（before/after）:
  - filter_ids bytes（raw → compressed）、mode 選択
  - filter_hi bytes（rANS → sparse）、ゼロ比率
  - total size delta (%)
- `bench_png_compare` カテゴリ差分
- decode時間差分
- 残課題
