# HakoNyans Phase 5 設計相談 — ChatGPT Pro へ

## 現状の成果（Phase 1-4 完了）

**HakoNyans** は高速デコード重視の次世代画像コーデックです。Phase 1-4 でエントロピー層（NyANS-P）を実装し、以下を達成しました：

### パフォーマンス実績（AMD Ryzen 9 9950X）
```
Single-core decode (LUT):        516 MiB/s  (2.80x vs baseline)
Multi-core decode (8 threads):  2366 MiB/s  (5.17x scaling)
Multi-core decode (16 threads): 2528 MiB/s  (5.52x scaling)

Overall speedup: 185 MiB/s → 2528 MiB/s (13.7x)
```

### 実装済みの技術
1. **rANS (Asymmetric Numeral Systems)** — 可逆エントロピーコーディング
2. **Flat Interleaved (N=8)** — 8状態が1本のビットストリームを共有 → SIMD向き
3. **Slot→Symbol LUT** — 4096エントリの O(1) テーブル（最大の効果：2.80x）
4. **AVX2 SIMD decoder** — gather命令による並列デコード
5. **P-Index (Parallel Index)** — チェックポイントでマルチコアスケーリング（効率83%）

### テスト状況
- 全23テスト完全パス（Phase 1: 5, Phase 2: 5, Phase 3: 4, Phase 4: 9）
- 80,000 シンボルの往復テスト（ビット完全一致）
- マルチスレッド正確性検証済み

---

## Phase 5 の目標

**画像コーデック統合**: RGB画像 → .hkn ファイル → RGB画像

```
[Encode]
RGB画像 → 前処理 → DCT → 量子化 → トークン化 → rANS → .hkn

[Decode] ← ここが最速であるべき
.hkn → rANS → 逆トークン化 → 逆量子化 → IDCT → 後処理 → RGB画像
```

---

## 質問 1: 圧縮前処理（Transform + Quantization）

Phase 1-4 のエントロピー層は **「小アルファベット + スキュー分布」に最適化済み**。
DCT係数をどう処理すれば、rANS が最高効率で動くか？

### 検討項目

**A. DCT バリアント**
- 8×8 DCT（JPEG互換） vs 16×16 / 32×32（大きいブロック = 高周波情報多い）
- 整数DCT（BinDCT, 固定小数点）vs 浮動小数点DCT
- 分離可能DCT（行→列）の SIMD 実装方針

**B. 量子化戦略**
- Uniform quantization（JPEG-like）
- Dead-zone quantization（低周波をより細かく）
- Perceptual quantization（視覚特性考慮）
- **ロスレスモード**：量子化ステップ Q=1 で完全可逆にできるか？

**C. スキャン順序**
- Zigzag（JPEG） vs Hilbert curve vs 適応的スキャン
- 低周波 → 高周波の順序が rANS の run-length に有利？

**D. 係数の符号化方式**
現在の実装（Phase 2 tokenization.h）:
```cpp
RUN(0..15)  // ゼロラン長
MAGC(0..11) // |v| のクラス（log2ベース）
SIGN        // 符号
REM         // 下位ビット（raw bits、rANS外）
EOB         // End of Block
```
これは JPEG のやり方に近いが、もっと良い方法は？

**質問**:
1. 「高速デコード」を最優先するとき、DCT/量子化/スキャンの **ベストプラクティス** は？
2. rANS に最適な係数表現は？（現在の RUN+MAGC+REM は妥当？）
3. SIMD に向く IDCT の実装（libjpeg-turbo 12-bit 最適化の経験あり）

---

## 質問 2: 色空間 + サブサンプリング

**A. 色変換**
- RGB → YCbCr（JPEG-like）
- RGB → YCoCg（可逆、整数演算のみ）
- RGB → ICtCp（HDR向け、Rec.2100）

**B. Chroma サブサンプリング**
- 4:4:4（サブサンプリングなし、ロスレス向け）
- 4:2:2 / 4:2:0（JPEG互換、ロッシー）

**質問**:
1. 「ロスレス/ロッシー両対応」を考えると、どの色空間が汎用的？
2. YCoCg は本当に整数だけで完全可逆？（libjpeg-turbo では YCbCr の整数演算で微小誤差あり）

---

## 質問 3: 圧縮後処理（Deblocking / Deringing）

量子化によるブロックノイズ/リンギングを軽減する後処理フィルタ。

**A. デブロッキングフィルタ**
- JPEG2000 style（ウェーブレット境界）
- H.264/AVC style（適応的強度調整）
- 簡易フィルタ（3×3 / 5×5 ガウシアン）

**B. SIMD 実装**
- AVX2 で 8ピクセル同時処理（libjpeg-turbo upsampling の経験あり）

**質問**:
1. **Phase 5 で必須？** それとも Phase 6 の品質改善で追加？
2. デコード速度に与える影響（目標: フィルタ込みでも 2 GB/s 維持）

---

## 質問 4: 将来拡張の設計

Phase 6 以降で追加したい機能：

**A. アルファチャンネル**
- RGB + Alpha（RGBA）
- 可逆アルファ（マスク画像）

**B. HDR 対応**
- 10-bit / 12-bit チャンネル深度
- Rec.2100 / PQ / HLG

**C. プログレッシブデコード**
- 低解像度プレビュー → フル解像度（JPEG progressive-like）

**D. タイル並列**
- 画像を複数タイルに分割（P-Index は既にタイル内並列を実現）
- タイル境界のアーティファクト対策

**質問**:
1. Phase 5 のビットストリーム設計で、これらの拡張を **後から追加しやすく** するポイントは？
2. FileHeader に入れるべきメタデータ（現在: magic, version, width, height, colorspace, tiling, transform, quant）

---

## 質問 5: 競合との差別化

HakoNyans の **勝ち筋** は？

**競合分析**:
| Codec | Strengths | Weaknesses |
|-------|-----------|-----------|
| JPEG | 高速、普及 | 品質低い、ブロックノイズ |
| WebP | 品質良い | デコード遅い（Huffman） |
| AVIF | 最高品質 | **デコード激遅**（AV1 intra） |
| JPEG XL | 高品質+ロスレス | 複雑、SIMD最適化難しい |

**HakoNyans の強み**:
- ✅ rANS（Huffman より高速）
- ✅ P-Index（マルチコアスケーリング）
- ✅ LUT（O(1) シンボル検索）
- ✅ SIMD-friendly 設計

**質問**:
1. 「AVIF品質 × JPEG速度」を実現するための、圧縮前処理のキーテクニックは？
2. rANS の性能を活かすため、係数の **エントロピー分布を最適化** する手法は？
   （例: コンテキスト適応、適応量子化、予測符号化）

---

## 質問 6: 実装優先度

Phase 5 で **最小限実装すべき機能** と、**Phase 6 以降に延期していいもの** は？

**最小構成の候補**:
- RGB → YCbCr (4:2:0) → 8×8 DCT → 量子化（Q固定） → rANS → .hkn
- ロスレスは Phase 6 に延期

**vs フル機能**:
- ロスレス/ロッシー両対応
- 可変量子化（quality parameter）
- デブロッキングフィルタ
- プログレッシブ

どちらが正しい戦略？

---

## 技術スタック（参考）

- **言語**: C++20
- **SIMD**: AVX2（Tier 1）、AVX-512（Tier 2、将来）、NEON（Apple Silicon対応予定）
- **アーキテクチャ**: Box Theory（レイヤー独立、A/B テスト可能）
- **ベンチマーク環境**: AMD Ryzen 9 9950X (16コア)、GCC 13.3.0、-O3 -march=native

---

## まとめ: ChatGPT Pro への依頼

上記を踏まえて、以下を提案してください：

1. **Phase 5 の具体的アルゴリズム設計**（DCT/量子化/色変換/スキャン順序）
2. **ビットストリーム仕様**（.hkn ファイルフォーマット、拡張性考慮）
3. **実装順序**（何から作るべきか、Phase 6 に延期していいもの）
4. **差別化ポイント**（AVIF/JPEG XL に勝つ技術的根拠）

「**デコード最速 × 品質良い × 将来拡張可能**」の三兎を追える設計をお願いします 🐱
