# Phase 9k: Tile Match 4x4 実装指示書

**Date**: 2026-02-11  
**目的**: Lossless 圧縮率を改善しつつ、デコード速度の優位を維持する（`+2ms` 以内）

---

## 1. 先にやること（Phase 9i-2 検証）

まず現行（Phase 9j-2）の基準値を固定する。

### 1-1. 必須コマンド

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/bench_png_compare
./build/bench_decode
```

### 1-2. 成功基準（9i-2）

- `ctest`: **17/17 PASS**
- `bench_png_compare`（13画像）: UI/Anime/Game/Photo/Natural で明確な回帰なし
- `bench_decode`:
  - 現状比 **+2.0ms 以内**
  - かつ **+5% 以内**

---

## 2. Phase 9k 実装仕様（Tile Match 4x4 限定）

## 2-1. スコープ

- 対象: **Lossless のみ**
- 粒度: **8x8 ブロックを 4x4 x 4 分割した完全一致参照**
- 制約:
  - **4x4 限定**
  - **同一タイル内のみ**
  - **因果参照のみ**（未来参照禁止）
  - **固定候補テーブルのみ**（総当たり禁止）

## 2-2. 新モード定義

- `FileHeader::BlockType` に `TILE_MATCH4 = 3` を追加
- 既存の2bit block_type符号化をそのまま利用

## 2-3. 候補ベクトル

`(-4,0),(0,-4),(-4,-4),(4,-4),(-8,0),(0,-8),(-8,-8),(8,-8),(-12,0),(0,-12),(-12,-4),(-4,-12),(-16,0),(0,-16),(-16,-4),(-4,-16)`

- 16候補固定（4bit index）
- 近距離優先

## 2-4. 適用条件

8x8 ブロック内の 4 つの 4x4 すべてで、候補ベクトルのいずれかに**完全一致**した場合のみ採用。  
1つでも不一致なら `TILE_MATCH4` 不採用（既存 Copy/Palette/Filter へ戻す）。

## 2-5. ビットストリーム（lossless tile）

### ヘッダ拡張

- 既存 lossless tile header 32B の `hdr[7]`（reserved）を `tile4_data_size` として使用

### tile4_data 形式

- `TILE_MATCH4` ブロック数 `N` に対し、`2 bytes * N`
- 各ブロック 2 bytes:
  - `q0_idx` (low nibble), `q1_idx` (high nibble)
  - `q2_idx` (low nibble), `q3_idx` (high nibble)

### バージョン

- `src/codec/headers.h`
  - `VERSION` を `0x0005` へ更新
  - `MIN_SUPPORTED_VERSION` は維持（旧ファイル読込は継続）
- デコーダは `version < 0x0005` の場合、`tile4_data_size=0` として旧経路を使用

---

## 3. 変更対象ファイル

- `src/codec/headers.h`
  - `BlockType::TILE_MATCH4` 追加
  - version更新
- `src/codec/encode.h`
  - 4x4 Tile Match 候補探索
  - mode決定へ `TILE_MATCH4` 候補を追加
  - `tile4_data` 生成と tile header `hdr[7]` 出力
- `src/codec/decode.h`
  - `tile4_data_size` 読み取り
  - `TILE_MATCH4` ブロック再構成を追加
- `bench/bench_bit_accounting.cpp`
  - `TILE4` 行を追加（bytes / %）
- `tests/test_lossless_round2.cpp`
  - `TILE_MATCH4` を含む roundtrip 検証追加

---

## 4. 実装ガード（必須）

- 候補探索は固定16候補のみ
- 4x4一致チェックは short-circuit（不一致で即 break）
- `TILE_MATCH4` 選択は推定ビット最小化に統合
- 悪化防止:
  - 迷う場合は既存モード優先（Filter fallback）

---

## 5. 検証手順

## Step 1: Build/Test

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

期待: **17/17 PASS**

## Step 2: Lossless 圧縮比較

```bash
./build/bench_png_compare
./build/bench_bit_accounting test_images/ui/vscode.ppm --lossless
./build/bench_bit_accounting test_images/anime/anime_sunset.ppm --lossless
./build/bench_bit_accounting test_images/game/retro.ppm --lossless
```

確認:
- `TILE4` の発生が UI/Anime/Game で確認できる
- UI/Anime/Game でサイズ改善傾向
- Photo/Natural で大きな悪化なし

## Step 3: Decode 速度

```bash
./build/bench_decode
```

成功条件:
- 現行基準から **+2.0ms 以内**
- かつ **+5% 以内**

---

## 6. 成功基準（Phase 9k）

- ✅ `ctest 17/17 PASS`
- ✅ `TILE_MATCH4` で roundtrip 完全一致
- ✅ UI/Anime/Game の lossless サイズが改善（目安 `-2%〜-8%`）
- ✅ `bench_decode` が `+2ms` / `+5%` 以内

---

## 7. レポート形式（提出テンプレ）

1. 変更ファイル一覧  
2. 実装要点（探索範囲、候補数、符号化形式）  
3. テスト結果（17/17）  
4. ベンチ結果（before/after: UI/Anime/Game/Photo/Natural）  
5. デコード速度（before/after, 差分ms, 差分%）  
6. 残課題（Phase 9k-2 へ送る項目）

---

## 8. コミットメッセージ例

`Phase 9k: Implement 4x4 tile-match mode for lossless blocks`

---

## 9. ChatGPT/Gemini 依頼文（コピペ用）

Phase 9k 実装をお願いします。

目標:
- Lossless で 4x4 Tile Match を追加して UI/Anime/Game の圧縮率を改善
- デコード速度の悪化を +2ms 以内（かつ +5%以内）に抑える

必須仕様:
1. 先に Phase 9i-2 検証（ctest 17/17, bench_png_compare, bench_decode）で基準値固定
2. Lossless に `BlockType::TILE_MATCH4=3` を追加
3. 8x8 を 4x4x4 に分割し、4領域すべてが固定16候補ベクトルで完全一致した場合のみ採用
4. tile header `hdr[7]` を `tile4_data_size` として使い、各ブロック2byteで4つの候補indexを格納
5. `version 0x0005` に更新し、`version<0x0005` は旧経路で後方互換維持
6. bench_bit_accounting に TILE4 の内訳を追加
7. tests に TILE_MATCH4 roundtrip 検証を追加

完了後:
- build/test/bench の結果を提出
- before/after のサイズ・速度差分を表で報告
- コミットメッセージ:
  `Phase 9k: Implement 4x4 tile-match mode for lossless blocks`
