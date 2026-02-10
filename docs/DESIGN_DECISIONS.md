# HakoNyans 設計判断記録

このドキュメントは、Phase 5 設計レビューで発見・修正した問題と設計判断を記録する。

---

## Phase 5 設計レビュー（2026-02-10）

### 問題 1: CDF が単一 → 圧縮効率が悪い ❌

**発見**: Phase 1-4 の rANS 実装は CDF が 1本だけ。ZRUN(64種) と MAGC(12種) を同じアルファベットに混ぜると分布が汚れる。

**原因**: トークン全体を 1つの記号空間として扱っていた。

**解決**: **CDF を用途別に分離**
```cpp
CDF_dc_magc[12]      // DC 専用
CDF_ac_zrun[64]      // AC ZRUN 専用（EOB=63 が大多数）
CDF_ac_magc_lf[12]   // AC 低周波 MAGC（pos < 16）
CDF_ac_magc_hf[12]   // AC 高周波 MAGC（pos >= 16）
```

**効果**: 
- ZRUN の EOB 偏重を活かせる（EOB=63 が 80% 以上のブロックも多い）
- MAGC の周波数帯域別分布を活かせる（低周波は MAGC_3〜5、高周波は MAGC_0〜2）
- 推定圧縮率改善: **5〜10%**（JPEG Huffman の多テーブル戦略と同等）

**実装影響**: デコーダは文法固定（ZRUN → MAGC → ZRUN）なので CDF 切り替えは分岐 1回。速度影響なし。

---

### 問題 2: DC が AC と混在 → DPCM 実装不可 ❌

**発見**: Phase 2 の tokenization.h は coeffs[0]（DC）も coeffs[1..63]（AC）も一緒にトークン化。

**原因**: JPEG の「DC/AC 分離 + DC DPCM」の設計を反映していなかった。

**解決**: **DC と AC を別ストリームに分離**
```
PlaneStream:
  ├─ DC stream  (DPCM, MAGC/SIGN/REM のみ、ZRUN なし)
  └─ AC stream  (ZRUN/MAGC/SIGN/REM, P-Index 並列)
```

**効果**:
- DC DPCM がチャンク境界でリセット可能（P-Index 並列デコードと整合）
- DC と AC で文法が違う（DC は ZRUN 不要）→ 別 CDF が自然
- DC ストリームは小さい（全ブロック数 × 1係数）→ 逐次デコードでもボトルネックにならない

**実装影響**: エンコーダ・デコーダともに DC/AC 分離が必要。

---

### 問題 3: SIGN が rANS シンボル → 無駄 ❌

**設計書**: 「SIGN は raw 1bit（rANS 外）」  
**Phase 2 実装**: `SIGN_POS / SIGN_NEG` が TokenType enum に含まれる（rANS シンボルとして扱われる）

**原因**: 実装が設計書を反映していなかった。

**解決**: **SIGN と REM を完全に raw bits に統一**
```cpp
// rANS で扱うのは ZRUN と MAGC だけ
enum TokenType {
    ZRUN_0..ZRUN_63,   // 64種
    MAGC_0..MAGC_11,   // 12種
};

// SIGN と REM は bitstream に直接書き込み（rANS バイパス）
write_bit(sign);
write_bits(rem, magc);
```

**効果**:
- SIGN は 50/50 分布 → rANS に通してもエントロピー削減ゼロ（むしろ overhead）
- REM も均等分布 → raw bits のほうが高速
- rANS のアルファベットサイズ削減: 32種 → 76種（CDF テーブル削減）

**実装影響**: tokenization.h を全面書き換え。

---

### 問題 4: パディング仕様が未定義 ❌

**発見**: 幅/高さが 8 の倍数でない場合の処理が SPEC.md に記載なし。

**解決**: **ミラーパディング（edge replication）を採用**
```
エンコード: 右端/下端を最終ピクセルで複製
デコード:   元のサイズ [0..width-1, 0..height-1] をクロップ
```

**採用理由**:
- ゼロパディングよりもブロック境界アーティファクトが少ない
- JPEG/WebP と同じ方式（互換性の観点で安全）

**実装影響**: 軽微（エンコーダにパディング処理追加、デコーダにクロップ追加）

---

### 問題 5: タイル寸法計算式が未定義 ❌

**発見**: tile_cols / tile_rows から各タイルのサイズを計算する式が SPEC.md に記載なし。

**解決**: **パディング後のサイズでタイル分割**
```
pad_w = ceil(width / 8) * 8
pad_h = ceil(height / 8) * 8

tile_w = ceil(pad_w / tile_cols)
tile_h = ceil(pad_h / tile_rows)
```

**Phase 5 制約**: tile_cols=1, tile_rows=1 固定（タイル分割なし）

**Phase 6**: 大画像向けタイル分割実装（4K で 2×2 など）

---

## 設計判断：前後サンドイッチ戦略

### 背景
ユーザーの要求「主戦場は全部（写真・スクショ・線画）」に対し、後段フィルタだけでは不十分。

### 判断
**Phase 5.5 で前段（適応量子化）、Phase 6.1 で後段（軽量フィルタ）を順次追加**

| フェーズ | 前段 | 後段 | デコーダ負荷 | 用途 |
|---------|------|------|------------|------|
| Phase 5 | 固定 Q | なし | **最速** | 高速優先 |
| Phase 5.5 | **Activity masking** | なし | 変わらず | 写真/スクショ自動判別 |
| Phase 6.1 | Activity masking | **軽量 EPF** | +5〜10% | 低ビットレート品質 |

### Activity Masking の自動判別
```cpp
// 画像全体の統計から自動判別
if (median_variance < 100 && edge_ratio > 0.3) {
    // 線画・スクショモード
    edge_weight = 2.0;         // エッジ近傍は丁寧に
    texture_weight = 0.5;      // テクスチャ許容しない
} else {
    // 写真モード
    edge_weight = 1.0;
    texture_weight = 1.5;      // テクスチャ部分は粗くてOK
}
```

### 差別化ポイント
- **Phase 5.5**: デコーダ負荷ゼロのまま画質改善 → 「JPEG 速度のまま JPEG より綺麗」
- **Phase 6.1**: 低ビットレート特化 → 「AVIF 品質に近づく」

---

## 採用した学術的手法

### 1. Interleaved rANS (Phase 1-4 完了)
**出典**: Giesen, "Interleaved Entropy Coders" (arXiv:1402.3392)  
**HakoNyans への適用**: N=8 状態、1本ビットストリーム、LUT デコード

### 2. Decoder-Adaptive Parallelism (Phase 4 完了)
**出典**: Bamberger et al., "Recoil: Parallel rANS Decoding" (arXiv:2306.12141)  
**HakoNyans への適用**: P-Index（中間状態スナップショット）、任意スレッド数に対応

### 3. Perceptual Vector Quantization (Phase 5.5 予定)
**出典**: Egge & Daede, "Perceptual Vector Quantization for Video Coding" (arXiv:1602.05209)  
**HakoNyans への適用**: Activity masking によるブロック単位 ΔQ、写真/線画自動判別

### 4. Edge-Preserving Filter (Phase 6.1 予定)
**出典**: JPEG XL Whitepaper (ds.jpeg.org)  
**HakoNyans への適用**: 軽量 EPF（optional、normative にしない）、quant step から強度導出

---

## 今後の懸念事項

### ⚠️ CDF 構築の overhead
- Phase 5.5 で 4種類の CDF（dc_magc, ac_zrun, ac_magc_lf, ac_magc_hf）
- エンコーダ側の頻度カウント → 正規化コストが増える
- **対策**: CDF は plane 単位で共有（全ブロックで 1つの CDF）

### ⚠️ Activity masking のエンコード速度
- ブロック単位の分散/エッジ/テクスチャ計算
- **対策**: 画素ロード時に同時計算（キャッシュ効率化）、SIMD 化

### ⚠️ Per-block quant table のメモリ
- Full HD: 1920×1080 / 64 = 32,400 ブロック × 64 u16 = 4 MB
- **対策**: Δq を i8 差分で圧縮（32,400 bytes = 32 KB）

---

## 参考文献

1. Duda, "Asymmetric Numeral Systems" (arXiv:0902.0271)
2. Giesen, "Interleaved Entropy Coders" (arXiv:1402.3392)
3. Bamberger et al., "Recoil: Parallel rANS Decoding" (arXiv:2306.12141)
4. Egge & Daede, "Perceptual Vector Quantization for Video Coding" (arXiv:1602.05209)
5. Malvar et al., "Lifting-based Reversible Color Transforms" (Microsoft Research, 2008)
6. JPEG XL Whitepaper (ds.jpeg.org)
7. AV1 Technical Overview (AOMedia)
8. WebP Compression Techniques (Google Developers)
