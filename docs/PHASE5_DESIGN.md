# HakoNyans Phase 5 設計書

**Status**: ChatGPT Pro レビュー済み + Copilot CLI 補足  
**Date**: 2025-02-10

---

## 設計方針: "爆速デコード最優先の最小コーデック"

### コア原則
1. **デコーダを単純に保つ** — 複雑さはエンコーダに寄せる
2. **Phase 1-4 の NyANS-P を最大限活かす** — 並列エントロピーが最大の武器
3. **後から拡張できる箱を仕込む** — Phase 6 で品質改善を足せる構造

---

## 1. Transform: 8×8 固定 DCT

### 採用理由
- SIMD 最適化資産が圧倒的（libjpeg-turbo 12-bit 最適化の経験を直接活かせる）
- 係数分布が rANS の小アルファベット設計と噛み合う
- 16×16/32×32 は Phase 6 で検討（VarDCT 路線）

### 実装方針
- **分離可能 1D×2**（行変換 → 列変換）
- **整数 DCT**（固定小数点、AAN スケーリング統合可能）
- **AAN スケール要素と Quant を事前合成** → デコード時の余計なスケーリングを排除

### IDCT (デコード側)
```
// 行方向 1D IDCT × 8行 → 列方向 1D IDCT × 8列
// AVX2: 8×8 ブロックを __m256i × 8 で保持
// 転置は _mm256_unpacklo/hi + permute
```

---

## 2. Quantization: JPEG-like 位置依存 Quant Matrix

### Phase 5 仕様
- **8×8 Quant Matrix** × quality スケール
- デコーダ側: `coeff = token_value * deq_table[zigzag_pos]`（単純乗算）
- Quant テーブルはタイル/チャンク内で固定（並列デコードと整合）

### ロスレスモード（Phase 6）
- Q=1 固定 + 可逆色変換（YCoCg-R）で完全可逆
- 整数 DCT の丸め誤差を排除するため、ロスレスは予測符号化に切り替えも検討

### Quality パラメータ
- `quality` = 1..100（JPEG 互換の直感的スケール）
- 内部: `deq[k] = base_quant[k] * scale_factor(quality)`

---

## 3. Scan Order: Zigzag (固定)

- 低周波→高周波で並び、末尾にゼロが溜まりやすい
- EOB/RUN と相性最良
- 固定 LUT 一発（分岐ゼロ）

```cpp
static constexpr int zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};
```

---

## 4. トークン化改善: ZRUN 統合 + CDF 分離

### 現行 (Phase 2)
```
RUN(0..15) → MAGC(0..11) → SIGN → [REM raw bits]
EOB
```

### 改善案 (Phase 5)
```
ZRUN(0..63)  ← RUN + EOB を統合（次の非ゼロまでのゼロ数 or 末尾）
MAGC(0..11)  ← |v| のクラス (band_group 別に分布を分ける)
SIGN         ← raw 1bit（rANS 外）
REM          ← raw bits（rANS 外）
EOB = ZRUN(残り全部ゼロ) ← ZRUN の特殊値として表現
```

### 利点
- **トークン数削減**: RUN + EOB の2トークン → ZRUN 1トークン
- **rANS 負荷削減**: SIGN と REM は raw bits（rANS に通さない → 分岐ゼロ）
- **band_group 別 MAGC**: 低周波/高周波で CDF を分ける → エントロピー効率 UP
- **CDF 分離**: ZRUN と MAGC で別 CDF → 分布の偏りを活かせる

### ZRUN 詳細
```
ZRUN = 0     : 非ゼロ係数が直後（ゼロラン長 0）
ZRUN = 1..62 : ゼロが N 個続いた後に非ゼロ
ZRUN = 63    : EOB（残り全部ゼロ）
```
合計 64 エントリ → rANS の小アルファベット（LUT に収まる）

### CDF 分離戦略（圧縮効率最適化）

デコーダは文法固定（ZRUN → MAGC → ZRUN → ...）なので、**CDF を用途別に分ける**：

```cpp
CDF_zrun[64]     // ZRUN 専用（EOB=63 が大多数 → 偏りが大きい）
CDF_magc_lf[12]  // 低周波 MAGC（MAGC_3〜5 が多い）
CDF_magc_hf[12]  // 高周波 MAGC（MAGC_0〜2 が多い）
```

**デコード手順**:
```
while (pos < 63) {
    zrun = decode_symbol(CDF_zrun);   // ← ZRUN 用 CDF
    if (zrun == 63) break;            // EOB
    pos += zrun;
    
    cdf = (pos < 16) ? CDF_magc_lf : CDF_magc_hf;
    magc = decode_symbol(cdf);        // ← MAGC 用 CDF（band別）
    sign = read_bit();                // ← raw bit
    rem = read_bits(magc);            // ← raw bits
    coeffs[pos++] = reconstruct(magc, sign, rem);
}
```

**効果**: ZRUN の EOB 偏重 + MAGC の band 別分布を両方活かせる → **圧縮率 UP**

---

## 5. 色空間

### Phase 5: YCbCr 4:4:4（整数近似）
- RGB → YCbCr（BT.601 整数近似）
- サブサンプリングなし → デコーダが単純
- 各チャンネルを独立に DCT → 量子化 → rANS

### Phase 6: ロスレス対応時
- **YCoCg-R**（Malvar リフティング、完全可逆）
```cpp
// Forward (可逆)
Co = R - B;
t  = B + (Co >> 1);  // 算術右シフト
Cg = G - t;
Y  = t + (Cg >> 1);

// Inverse (可逆)
t  = Y - (Cg >> 1);
G  = Cg + t;
B  = t - (Co >> 1);
R  = Co + B;
```

### Phase 6: サブサンプリング
- 4:2:0 / 4:2:2 をオプション追加
- アップサンプルは AVX2 で SIMD 化（libjpeg-turbo h2v2 の経験を活用）

---

## 6. DC 係数: チャンク内 DPCM（別ストリーム）

### 方針
- **DC は AC と別管理**（DCT 後に分離）
- DC 係数だけチャンク内で差分予測（DPCM）
- **P-Index のチャンク境界でリセット** → 並列デコードを壊さない

### DC ストリーム構造
```
チャンク先頭:  DC[0] = 絶対値（そのまま符号化）
チャンク内:    DC[n] = DC[n] - DC[n-1]（差分を符号化）

DC 符号化:
  MAGC(0..11) → SIGN (raw 1bit) → REM (raw bits)
  （ZRUN は不要、DC は必ず存在）
```

### AC ストリーム構造
```
AC[1..63] → ZRUN/MAGC ストリーム（前述の通り）
```

### PlaneStream 内レイアウト
```
┌─────────────────────┐
│ DC_STREAM           │ ← MAGC/SIGN/REM のみ（ZRUN なし）
│   per chunk:        │
│     DC[0] 絶対値     │
│     DC[1..N-1] 差分 │
├─────────────────────┤
│ AC_STREAM           │ ← ZRUN/MAGC/SIGN/REM
│   + P-INDEX         │
└─────────────────────┘
```

**DC と AC を分離する理由**:
1. DC は DPCM → チャンク境界リセットが必要
2. AC は並列デコード → P-Index で分割
3. 文法が違う（DC は ZRUN 不要）→ 別 CDF が効率的

---

## 7. .hkn ビットストリーム v0.2

### Box/Chunk コンテナ
未知の box はスキップ可能 → 将来拡張に強い

```
┌──────────────────────────────────┐
│ FileHeader (固定長 48B)           │
├──────────────────────────────────┤
│ ChunkDirectory                   │
│   chunk_count: u32               │
│   entries[]:                     │
│     type:   4B (ASCII)           │
│     offset: u64                  │
│     size:   u64                  │
├──────────────────────────────────┤
│ Chunk: QMAT (量子化テーブル)      │
│   quality: u8                    │
│   quant_y[64]:  u16              │
│   quant_cb[64]: u16 (optional)   │
│   quant_cr[64]: u16 (optional)   │
├──────────────────────────────────┤
│ Chunk: TILE[0]                   │
│   PlaneStream Y:                 │
│     TOKENS (NyANS-P bitstream)   │
│     PINDEX (parallel checkpoints)│
│   PlaneStream Cb:                │
│     TOKENS + PINDEX              │
│   PlaneStream Cr:                │
│     TOKENS + PINDEX              │
├──────────────────────────────────┤
│ Chunk: TILE[1] ...               │
├──────────────────────────────────┤
│ Chunk: META (optional)           │
│   ICC profile, EXIF, etc.        │
├──────────────────────────────────┤
│ Chunk: AUXC (Phase 6: alpha等)   │
└──────────────────────────────────┘
```

### FileHeader v0.2
| Field | Size | Description |
|-------|------|-------------|
| magic | 4B | `HKN\0` (0x484B4E00) |
| version | 2B | 0x0002 (v0.2) |
| flags | 2B | bit0: lossy/lossless, bit1-15: reserved |
| width | 4B | 画像幅 (pixels) |
| height | 4B | 画像高 (pixels) |
| bit_depth | 1B | 8, 10, 12, 16 |
| num_channels | 1B | 1=Gray, 3=YCbCr, 4=RGBA |
| colorspace | 1B | 0=YCbCr, 1=YCoCg-R, 2=RGB |
| subsampling | 1B | 0=4:4:4, 1=4:2:2, 2=4:2:0 |
| tile_cols | 2B | タイル列数 |
| tile_rows | 2B | タイル行数 |
| block_size | 1B | 8 (固定、将来 16/32 用) |
| transform_type | 1B | 0=DCT-II |
| entropy_type | 1B | 0=NyANS-P |
| interleave_n | 1B | rANS 状態数 (8) |
| pindex_density | 1B | 0=none, 1=64KB, 2=16KB, 3=4KB |
| quality | 1B | 1..100 (0=lossless) |
| reserved | 14B | 将来拡張用 |
| **Total** | **48B** | |

---

## 8. デコードパイプライン

```
.hkn file
  │
  ├─ FileHeader 読み込み (48B)
  ├─ ChunkDirectory 読み込み
  ├─ QMAT 読み込み → deq_table 構築
  │
  ├─ TILE[n] ごとに（タイル並列可能）:
  │   ├─ PlaneStream ごとに（Y/Cb/Cr）:
  │   │   ├─ P-Index 読み込み
  │   │   ├─ P-Index でチャンク分割 → スレッド並列
  │   │   │   ├─ NyANS-P decode (LUT or AVX2)
  │   │   │   ├─ Detokenize (ZRUN → 係数)
  │   │   │   └─ Dequantize (coeff * deq_table[k])
  │   │   └─ DC DPCM 復元（チャンク先頭は絶対値）
  │   │
  │   └─ 8×8 IDCT (AVX2 SIMD)
  │
  └─ 色変換 YCbCr → RGB (AVX2 SIMD)
      │
      └─ RGB 出力
```

---

## 9. 後処理フィルタ（Phase 6）

### Phase 5: なし（normative なフィルタは入れない）
### Phase 6 で検討:
- **Optional deblocking** — quality/quant から強度自動導出
- **境界のみ、1D、固定タップ** — 軽量に限定
- normative にしない（ビットストリームの"正解画"は固定しない）
- 目標: フィルタ込みでも 2 GB/s 維持

---

## 10. 実装順序（Phase 5 ロードマップ）

### Step 1: Grayscale end-to-end（最小動作確認）
- [ ] 8×8 DCT / IDCT（スカラー、後で SIMD 化）
- [ ] 量子化 / 逆量子化（固定 quant matrix）
- [ ] Zigzag scan
- [ ] DC/AC 分離 + DPCM
- [ ] ZRUN トークン化（改善版、CDF 分離対応）
- [ ] .hkn ファイル書き出し / 読み込み（Grayscale only）
- [ ] Grayscale 画像の往復テスト（PSNR 計測）

### Step 2: Color（YCbCr 4:4:4）
- [ ] RGB → YCbCr 変換（整数近似）
- [ ] 3 チャンネル独立エンコード / デコード
- [ ] カラー画像の往復テスト

### Step 3: 統合 + ベンチマーク
- [ ] タイル分割（大画像対応）
- [ ] P-Index 並列デコード統合
- [ ] CLI ツール (`hakonyans encode/decode/info`)
- [ ] Full HD / 4K ベンチマーク
- [ ] JPEG / libjpeg-turbo との速度・品質比較

---

## 11. Phase 5.5: 前段非可逆（適応量子化）

**目標**: エンコーダに activity masking 追加（デコーダ変更なし）

### 適応量子化（Activity Masking）

エンコーダが画像解析して、**ブロックごとに量子化ステップを調整**：

```
平坦領域（空・肌）   → Q × 0.8  （丁寧に、アーティファクトが目立つ）
テクスチャ（草・髪） → Q × 1.2  （粗くても目立たない）
エッジ近傍           → Q × 0.9  （リンギング抑制）
```

### デコーダへの影響：**ゼロ**

```cpp
// デコーダは変わらない（quant table を読んで掛けるだけ）
coeff = token_value * deq_table[block_id][zigzag_pos];
```

### 実装方針

#### エンコーダ側（Phase 5.5 で追加）
```cpp
1. ブロック単位で activity 計算（分散/エッジ強度/テクスチャ度）
2. activity → ΔQ マップ（-20% 〜 +50%）
3. base_quant × ΔQ → per-block quant table
4. quant table を QMAT チャンクに書き込み（可変長）
```

#### ビットストリーム拡張
```
QMAT Chunk v2:
  quality: u8
  quant_mode: u8  ← 0=uniform, 1=per-block adaptive
  
  if (quant_mode == 0):
    quant_table[64]: u16   ← 1個だけ
  
  if (quant_mode == 1):
    num_blocks: u32
    delta_q[num_blocks]: i8  ← 差分（-127..+127, 0.5% 刻み）
    base_quant_table[64]: u16
    // デコーダ: deq[block][k] = base[k] * (1.0 + delta_q[block]/200.0)
```

### 判別アルゴリズム（3種を組み合わせ）

| 特徴 | 計算式 | 判定 |
|------|--------|------|
| 平坦度 | `variance < threshold` | 平坦 → Q を下げる |
| テクスチャ度 | `Laplacian energy` | 高 → Q を上げる |
| エッジ近傍 | `Sobel gradient` | 高 → Q を下げる（リンギング防止） |

**写真 vs スクショ/線画の自動判別**:
```cpp
// 画像全体で判定
if (median_variance < 100 && edge_ratio > 0.3) {
    // 線画・スクショモード（エッジ重視）
    edge_weight = 2.0;
} else {
    // 写真モード（テクスチャ許容）
    texture_weight = 1.5;
}
```

### Phase 5.5 追加実装

- [ ] `src/codec/activity_masking.h` — ブロック activity 計算
- [ ] `src/codec/quant.h` — per-block quant table 生成
- [ ] QMAT Chunk v2 対応（quant_mode 追加）
- [ ] エンコーダに activity masking 統合
- [ ] `HAKONYANS_DISABLE_MASKING` 環境変数（A/B テスト用）
- [ ] PSNR/SSIM が同じ時のファイルサイズ比較

---

## 11. Phase 6 延期リスト

以下は Phase 5 では実装せず、Phase 6 以降で追加：

### Phase 6.1: 後段非可逆（軽量フィルタ）
- [ ] 軽量 EPF（Edge-Preserving Filter）または CDEF（方向性フィルタ）
- [ ] **normative にしない**（optional post-filter として仕様化）
- [ ] quant step から強度を自動導出（ビットストリームに制御情報を持たせない）
- [ ] `HAKONYANS_DISABLE_POSTFILTER` 環境変数（A/B テスト）
- [ ] 低ビットレート写真での主観評価（フィルタ有無比較）

### Phase 6.2: 高度な圧縮
- [ ] 4:2:0 / 4:2:2 サブサンプリング
- [ ] ロスレスモード（YCoCg-R + Q=1 or 予測符号化）
- [ ] プログレッシブデコード
- [ ] HDR / 10-12bit 対応
- [ ] アルファチャンネル（RGBA）
- [ ] 可変ブロックサイズ（16×16, 32×32）
- [ ] RDO（レート歪最適化）エンコーダ

### Phase 6.3: メタデータ
- [ ] ICC プロファイル / EXIF メタデータ
- [ ] 色管理（BT.601/BT.709/BT.2020）

---

## 12. 前後サンドイッチ戦略まとめ

| フェーズ | 前段（エンコーダ） | 後段（デコーダ） | デコーダ負荷 |
|---------|-------------------|-----------------|------------|
| Phase 5 | 固定 quant | なし | **最速** ✓ |
| Phase 5.5 | **適応量子化** | なし | **変わらず** ✓ |
| Phase 6.1 | 適応量子化 | **軽量 EPF** | +5〜10% |

**差別化ポイント**:
1. Phase 5.5 で「前段だけの知覚最適化」→ デコード爆速のまま画質 UP
2. Phase 6.1 で「前後サンドイッチ」→ 低ビットレートでも崩れない

これが **「JPEG 速度 × AVIF 品質」の現実解**にゃ。

---

## 参考文献

1. Duda, "Asymmetric Numeral Systems" (arXiv:0902.0271)
2. Giesen, "Interleaved Entropy Coders" (arXiv:1402.3392)
3. Bamberger et al., "Recoil: Parallel rANS Decoding" (arXiv:2306.12141)
4. Malvar et al., "Lifting-based Reversible Color Transforms" (Microsoft Research, 2008)
5. WebP Compression Techniques (developers.google.com)
6. AV1 Technical Overview (AOMedia)
7. Arai, Agui, Nakajima, "A fast DCT-SQ scheme for images" (1988)
