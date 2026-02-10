# HakoNyans Bitstream Specification

**Version**: 0.1 (Draft)
**Status**: Work in Progress

## 1. ファイル構成

```
┌──────────────┐
│ FileHeader   │  magic, version, dimensions, colorspace, params
├──────────────┤
│ TileTable    │  タイルオフセット一覧（ランダムアクセス用）
├──────────────┤
│ Tile 0       │
│  ├ LowFreq   │  低周波ストリーム（小さい、逐次OK）
│  └ HighFreq  │  高周波ストリーム
│     ├ Core   │  interleaved rANS ビットストリーム
│     └ PIndex │  並列復号チェックポイント
├──────────────┤
│ Tile 1       │
│  ...         │
└──────────────┘
```

## 2. FileHeader

| Field | Size | Description |
|-------|------|-------------|
| magic | 4B | `HKN\0` (0x484B4E00) |
| version | 2B | Major.Minor (0x0001 = v0.1) |
| width | 4B | 画像幅 (pixels) |
| height | 4B | 画像高 (pixels) |
| bit_depth | 1B | 8, 10, 12, 16 |
| colorspace | 1B | 0=YCbCr, 1=RGB, 2=Grayscale |
| tile_cols | 2B | タイル列数 |
| tile_rows | 2B | タイル行数 |
| transform_type | 1B | 0=DCT, 1=Reserved |
| entropy_type | 1B | 0=NyANS-P |
| interleave_n | 1B | rANS 状態数 (default: 8) |
| pindex_density | 1B | P-Index 密度クラス |
| reserved | 8B | 将来拡張用 |

## 3. TileTable

各タイルの `byte_offset` を格納。デコーダはここから任意タイルにジャンプ可能。

| Field | Size | Description |
|-------|------|-------------|
| tile_count | 4B | タイル総数 |
| offsets[] | 8B × N | 各タイルのバイトオフセット |

## 4. Tile 構造

### 4.1 LowFreqStream

低周波 DC 係数。量が少ないので逐次デコードでも問題なし。
energy_class 等のコンテキスト情報を確定するために先に復号する。

### 4.2 HighFreqStream

#### CoreBitstream
interleaved rANS の出力バイト列。詳細は [ENTROPY_NYANS_P.md](ENTROPY_NYANS_P.md) 参照。

#### P-Index
並列復号用チェックポイント。詳細は [PARALLEL_INDEX.md](PARALLEL_INDEX.md) 参照。

## 5. ファイル拡張子

`.hkn`

## 6. MIME Type (予定)

`image/x-hakonyans`
