# HakoNyans Bitstream Specification

**Version**: 0.2 (Draft)
**Status**: Work in Progress

## 1. ファイル構成（Box/Chunk コンテナ）

未知の Chunk はスキップ可能 → 将来拡張に強い。

```
┌───────────────────────────────────┐
│ FileHeader (固定長 48B)            │
├───────────────────────────────────┤
│ ChunkDirectory                    │
│   chunk_count: u32                │
│   entries[]:                      │
│     type:   4B (ASCII)            │
│     offset: u64                   │
│     size:   u64                   │
├───────────────────────────────────┤
│ Chunk: QMAT (量子化テーブル)       │
├───────────────────────────────────┤
│ Chunk: TILE[0]                    │
│   PlaneStream Y:                  │
│     TOKENS (NyANS-P bitstream)    │
│     PINDEX (parallel checkpoints) │
│   PlaneStream Cb:                 │
│     TOKENS + PINDEX               │
│   PlaneStream Cr:                 │
│     TOKENS + PINDEX               │
├───────────────────────────────────┤
│ Chunk: TILE[1] ...                │
├───────────────────────────────────┤
│ Chunk: META (optional, ICC/EXIF)  │
├───────────────────────────────────┤
│ Chunk: AUXC (future: alpha/HDR)   │
└───────────────────────────────────┘
```

## 2. FileHeader (48 bytes, fixed)

| Field | Size | Description |
|-------|------|-------------|
| magic | 4B | `HKN\0` (0x484B4E00) |
| version | 2B | 0x0002 (v0.2) |
| flags | 2B | bit0: 0=lossy, 1=lossless; bit1-15: reserved |
| width | 4B | 画像幅 (pixels) |
| height | 4B | 画像高 (pixels) |
| bit_depth | 1B | 8, 10, 12, 16 |
| num_channels | 1B | 1=Gray, 3=YCbCr, 4=RGBA |
| colorspace | 1B | 0=YCbCr, 1=YCoCg-R, 2=RGB |
| subsampling | 1B | 0=4:4:4, 1=4:2:2, 2=4:2:0 |
| tile_cols | 2B | タイル列数 |
| tile_rows | 2B | タイル行数 |
| block_size | 1B | 8 (固定; 将来 16/32 用) |
| transform_type | 1B | 0=DCT-II |
| entropy_type | 1B | 0=NyANS-P |
| interleave_n | 1B | rANS 状態数 (default: 8) |
| pindex_density | 1B | 0=none, 1=64KB, 2=16KB, 3=4KB |
| quality | 1B | 1..100 (0=lossless) |
| reserved | 14B | 将来拡張用 |

## 3. ChunkDirectory

| Field | Size | Description |
|-------|------|-------------|
| chunk_count | 4B | チャンク総数 |
| entries[] | 20B × N | type(4B) + offset(8B) + size(8B) |

### 定義済みチャンクタイプ
| Type | Description |
|------|-------------|
| `QMAT` | 量子化テーブル |
| `TILE` | タイルデータ（PlaneStream 含む） |
| `META` | ICC profile, EXIF 等 |
| `AUXC` | 補助チャンネル（alpha, HDR, depth） |

未知のタイプは `offset + size` でスキップ。

## 4. QMAT Chunk

| Field | Size | Description |
|-------|------|-------------|
| quality | 1B | 1..100 (quality scale) |
| num_tables | 1B | 1 or 3 |
| quant_table[] | 128B × N | u16[64] per table (zigzag order) |

Phase 5: `quality` からの quant matrix 導出: `deq[k] = base_quant[k] * scale_factor(quality)`

## 5. TILE Chunk

### 5.1 PlaneStream (per plane: Y, Cb, Cr)

| Field | Size | Description |
|-------|------|-------------|
| dc_stream_size | 4B | DC ストリームのバイト長 |
| ac_stream_size | 4B | AC ストリームのバイト長 |
| pindex_size | 4B | P-INDEX のバイト長 (0 = なし) |
| dc_stream[] | var | DC DPCM データ（MAGC+SIGN+REM） |
| ac_stream[] | var | AC NyANS-P bitstream（ZRUN+MAGC+SIGN+REM） |
| pindex[] | var | P-Index checkpoints（AC 用） |

データは 8-byte アライン。

### 5.2 トークン形式

#### DC ストリーム（DPCM、ZRUN なし）
```
per block:
  MAGC(0..11) → [SIGN: raw 1bit] → [REM: raw bits]
  
チャンク先頭 DC[0]: 絶対値
チャンク内 DC[n]:   DC[n] - DC[n-1]（差分）
P-Index チャンク境界でリセット
```

#### AC ストリーム（ZRUN 統合版）
```
ZRUN(0..63) → MAGC(0..11) → [SIGN: raw 1bit] → [REM: raw bits]
  ZRUN=0     : 直後に非ゼロ（ゼロラン長 0）
  ZRUN=1..62 : ゼロが N 個続く
  ZRUN=63    : EOB（残り全部ゼロ）
```

#### CDF 使い分け（圧縮効率最適化）
```cpp
// DC 用
CDF_dc_magc[12]      // DC は MAGC のみ（ZRUN なし）

// AC 用（周波数帯域別）
CDF_ac_zrun[64]      // ZRUN 専用（EOB=63 偏重）
CDF_ac_magc_lf[12]   // 低周波 AC（pos < 16）
CDF_ac_magc_hf[12]   // 高周波 AC（pos >= 16）
```

### 5.3 エンコード手順
```
1. 8×8 DCT → 係数[0..63]
2. DC/AC 分離:
     DC  → coeffs[0]
     AC  → coeffs[1..63]（zigzag scan）
3. DC  → DPCM → MAGC/SIGN/REM → DC stream
4. AC  → ZRUN/MAGC/SIGN/REM → NyANS-P encode → AC stream
5. AC stream に P-Index 構築
```

### 5.4 デコード手順
```
1. DC stream decode:
     MAGC/SIGN/REM → DC DPCM復元 → DC[0..N-1]
2. AC stream decode (P-Index 並列):
     ZRUN/MAGC/SIGN/REM → AC[1..63]
3. DC + AC → coeffs[0..63]
4. 逆量子化 → 8×8 IDCT
```

## 6. デコードパイプライン

```
parse FileHeader → parse ChunkDirectory → load QMAT
→ per TILE (parallel):
    per plane (Y/Cb/Cr):
      // DC ストリーム（逐次、小さい）
      decode DC stream → MAGC/SIGN/REM → DC DPCM restore → DC[0..N-1]
      
      // AC ストリーム（P-Index 並列、大きい）
      load P-Index → split to threads
      → NyANS-P decode (CDF_ac_zrun / CDF_ac_magc_lf/hf)
      → detokenize (ZRUN→AC[1..63])
      
      // 合成 → 変換
      → DC + AC → coeffs[0..63]
      → dequantize → 8×8 IDCT (SIMD)
→ YCbCr → RGB color conversion (SIMD)
→ output
```

## 7. 画像パディング仕様

幅/高さが 8 の倍数でない場合の処理：

### エンコード側
- 右端/下端を **ミラーパディング**（edge replication）
- パディング後のサイズ: `pad_w = ceil(width/8)*8`, `pad_h = ceil(height/8)*8`

### デコード側
- 全体をデコード後、`[0..width-1, 0..height-1]` をクロップ

**FileHeader には元のサイズ（width, height）を記録** → タイル分割は padded size で計算。

## 8. タイル分割仕様

```
tile_w = ceil(pad_w / tile_cols)
tile_h = ceil(pad_h / tile_rows)

各タイルのサイズは 8 の倍数（パディング済み）
最後の列/行のタイルは余りがあれば小さくなる
```

**Phase 5**: tile_cols=1, tile_rows=1 固定（タイル分割なし）  
**Phase 6**: 大画像向けタイル分割実装

## 9. ファイル拡張子

`.hkn`

## 10. MIME Type (予定)

`image/x-hakonyans`

## 11. 関連ドキュメント

- [PHASE5_DESIGN.md](PHASE5_DESIGN.md) — Phase 5 設計書（実装方針・優先順位）
- [ENTROPY_NYANS_P.md](ENTROPY_NYANS_P.md) — NyANS-P エントロピー層
- [PARALLEL_INDEX.md](PARALLEL_INDEX.md) — P-Index 並列復号仕様
- [SIMD_GUIDE.md](SIMD_GUIDE.md) — SIMD 実装ガイド
