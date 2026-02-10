# HakoNyans Benchmarks 📊

## PNG vs HKN Lossless Comparison (Phase 8b)

**Date**: Feb 11 2026
**Hardware**: x86_64 (AVX2 enabled)
**Test Conditions**: PNG level 9 vs HKN Lossless (YCoCg-R + filters)

### Overall Results

| Image | Category | PNG (KB) | HKN (KB) | Size Ratio | Enc Speedup | Dec Speedup |
|-------|----------|----------|----------|------------|-------------|-------------|
| browser | UI | 10.0 | 150.5 | 15.10x ❌ | 0.33x | 0.49x |
| vscode | UI | 11.4 | 155.2 | 13.60x ❌ | 0.37x | 0.47x |
| terminal | UI | 9.7 | 151.0 | 15.64x ❌ | 0.32x | 0.48x |
| anime_girl | Anime | 9.0 | 150.8 | 16.78x ❌ | 0.34x | 0.47x |
| anime_sunset | Anime | 10.4 | 153.0 | 14.73x ❌ | 0.33x | 0.50x |
| nature_01 | Photo | 1251.4 | 1153.8 | 0.92x ✅ | 4.74x | 0.39x |
| nature_02 | Photo | 1412.6 | 1232.5 | 0.87x ✅ | 9.01x | 0.40x |
| minecraft_2d | Game | 8.8 | 150.8 | 17.22x ❌ | 0.32x | 0.45x |
| retro | Game | 9.4 | 151.3 | 16.14x ❌ | 0.33x | 0.49x |
| kodim01 | Natural | 5.1 | 125.5 | 24.70x ❌ | 0.98x | 0.24x |
| kodim02 | Natural | 2.2 | 66.5 | 30.83x ❌ | 0.84x | 0.45x |
| kodim03 | Natural | 117.6 | 515.0 | 4.38x ❌ | 14.71x | 0.18x |
| hd_01 | Natural | 8.6 | 1040.9 | 121.33x ❌ | 0.31x | 0.24x |

### Category Analysis

| Category | Images | Avg Size Ratio | Avg Enc Speedup | Avg Dec Speedup |
|----------|--------|----------------|-----------------|-----------------|
| Anime | 2 | 15.75x ❌ | 0.33x | 0.49x |
| Game | 2 | 16.68x ❌ | 0.32x | 0.47x |
| Natural | 4 | 45.31x ❌ | 4.21x | 0.28x |
| Photo | 2 | 0.90x ✅ | 6.88x | 0.40x |
| UI | 3 | 14.78x ❌ | 0.34x | 0.48x |

### Analysis

#### ❌ Critical Issue Found
- **HKN Lossless has a serious bug**: File size is consistently ~150KB regardless of image content
- This indicates a problem with the lossless encoding implementation
- Only large natural photos (1MB+) show reasonable compression due to raw data size

#### ✅ Photo Category Win
- **Photo**: HKN achieves -10% size reduction and 6.88x encode speedup
- This is the only category where HKN performs as expected

#### 🔍 Investigation Required
- The ~150KB constant size suggests:
  1. Header overhead issue
  2. CDF serialization inefficiency (1024 bytes x 256 symbols = 256KB header!)
  3. Filter/encoding not being applied correctly

### Next Steps
- Debug lossless encoding (constant size issue)
- Optimize CDF storage for lossless mode
- Re-run benchmarks after fix

---

## Lossless Mode ベンチマーク (Phase 8)

**Date**: 2026-02-11
**Hardware**: x86_64 (AVX2 enabled)
**Test Conditions**: Lossless compression with YCoCg-R + PNG-compatible filters

### 圧縮結果

| 画像タイプ | Raw (KB) | HKN Lossless (KB) | 圧縮率 | エンコード (ms) | デコード (ms) |
|-----------|----------|-------------------|--------|----------------|--------------|
| Random 128×128 | 48.0 | 57.8 | 1.20x | 0.74 | 1.60 |
| Random 256×256 | 192.0 | 211.5 | 1.10x | 2.58 | 6.55 |
| **Gradient 256×256** | 192.0 | **33.8** | **0.18x** ✅ | 2.79 | 1.09 |
| **Solid 256×256** | 192.0 | **11.6** | **0.06x** ✅ | 2.49 | 0.76 |
| **UI Screenshot 320×240** | 225.0 | **35.4** | **0.16x** ✅ | 3.25 | 1.63 |
| Natural-like 256×256 | 192.0 | 161.2 | 0.84x | 2.36 | 3.57 |

### 分析

#### ✅ 高圧縮達成
- **UI/グラデーション/単色画像**: 84-94% 圧縮達成
- **自然画像**: 16% 圧縮（可逆圧縮としては良好）

#### ⚠️ ランダムデータ膨張
- **Random 画像**: 10-20% サイズ増加
- これは理論的に圧縮不可能なデータに対する正常挙動
- エントロピーが最大のデータは圧縮できない（情報理論の限界）

#### 🚀 デコード速度
- 単色/グラデーション: **0.76-1.09 ms** （極めて高速）
- UI画像: **1.63 ms** （実用的）
- 自然画像: **3.57 ms** （やや遅い、PNG比較は Phase 8b で実施）

### 技術詳細

- **色空間**: YCoCg-R（可逆整数変換）
- **フィルタ**: PNG互換 5種（None/Sub/Up/Average/Paeth）
- **並列化**: 256×256 タイル独立（将来的にマルチスレッド対応）
- **Screen Profile**: Palette/Copy モードをロスレスに統合可能

### 推奨事項

#### ✅ Lossless を使うべき場合
- UI/スクリーンショット（高圧縮）
- グラデーション画像（極めて高圧縮）
- 単色/ベタ塗り画像（最高圧縮）
- 完全可逆が必要な用途（医療、アーカイブ）

#### ❌ Lossless を避けるべき場合
- ランダム/ノイズが多い画像（膨張リスク）
- 高速エンコードが必要な場合（Lossy モードの方が高速）

### 次のステップ (Phase 8b)
- PNG との直接比較ベンチマーク
- マルチスレッドデコードの実装
- エンコード速度の最適化

---

## Screen Profile ベンチマーク (Phase 7c)

**Date**: 2026-02-11
**Hardware**: x86_64 (AVX2 enabled)
**Test Conditions**: Q75, 4:2:0, CfL enabled

### カテゴリ別サマリー

| カテゴリ | サイズ変化 | PSNR | エンコード | デコード | 評価 |
|---------|-----------|------|----------|----------|------|
| **UI Screenshots** | **-52.1%** ⭐ | +3.61 dB | 0.09x | 1.14x | 大成功 |
| **Game Screens** | +38.4% | +2.62 dB | 0.26x | 0.98x | 混合 |
| **Photos** | +36.9% | +5.08 dB | 0.04x | 1.10x | 悪化（想定内） |

### 詳細ベンチマーク結果

#### UI Screenshots (推奨)

| 画像 | ベースライン | Screen Profile | サイズ変化 | PSNR変化 | デコード |
|------|------------|---------------|-----------|---------|---------|
| browser | 464,277 B | 200,285 B | **-56.9%** | +6.21 dB | 1.13x |
| vscode | 422,487 B | 202,028 B | **-52.2%** | +1.75 dB | 1.13x |
| terminal | 377,288 B | 198,972 B | **-47.3%** | +2.87 dB | 1.16x |

#### Game Screenshots (混合)

| 画像 | ベースライン | Screen Profile | サイズ変化 | PSNR変化 | デコード |
|------|------------|---------------|-----------|---------|---------|
| minecraft_2d | 232,307 B | 211,704 B | -8.9% | +1.48 dB | 0.94x |
| retro | 108,602 B | 201,651 B | **+85.7%** ❌ | +3.75 dB | 1.02x |

#### Photos (非推奨)

| 画像 | ベースライン | Screen Profile | サイズ変化 | PSNR変化 | デコード |
|------|------------|---------------|-----------|---------|---------|
| kodim01 | 72,508 B | 99,145 B | +36.7% | +4.72 dB | 1.19x |
| kodim02 | 44,571 B | 45,772 B | +2.7% | +19.68 dB | 1.18x |
| kodim03 | 102,566 B | 113,298 B | +10.5% | -0.01 dB | 1.11x |
| hd_01 | 544,162 B | 1,075,822 B | **+97.7%** ❌ | -4.09 dB | 0.90x |

### 技術詳細

**Screen Profile の最適化技術**:
- **Palette Mode**: ≤8色のブロックを周波数ベースでインデックス化（Delta Palette符号化）
- **2D Copy Mode (IntraBC)**: 繰り返しパターンをSADベースでブロックマッチング（±64ブロック範囲）
- **自動モード選択**: Copy (完全一致) → Palette (≤8色) → DCT (デフォルト)

**ファイルフォーマット v2**:
- BlockType ストリーム（2-bit/block、RLE圧縮）
- Palette データストリーム
- Copy パラメータストリーム

### 推奨事項

#### ✅ Screen Profile を使うべき場合
- **UI スクリーンショット**: ブラウザ、エディタ、ターミナル、OS UI
- **テキスト大量の図版**: PDF、ドキュメント、プレゼンテーション
- **アイコン・ロゴ**: 繰り返しパターンが多い画像

**期待効果**: ファイルサイズ **-50%** 削減、PSNR +3.6 dB、デコード 1.14x

#### ❌ Screen Profile を避けるべき場合
- **写真・自然画像**: サイズ増加（+37%）、DCT圧縮が最適
- **グラデーション多いゲーム画像**: Palette/Copy モードが不適切
- **ノイズ・テクスチャが多い画像**: 繰り返しパターンなし

#### ⚠️ エンコード性能
- **0.09x** (UI), **0.26x** (Game) - 写真用途の **2-10倍遅い**
- オフライン用途・バッチ処理向け
- IntraBC検索（SAD計算）とPalette抽出がボトルネック

---

## Phase 7b: Speed Optimization Report

**Date**: 2026-02-10
**Image**: HD 1920x1080 (Natural-like Gradient)
**Hardware**: 16-thread CPU (AVX2 enabled)

### 1. Decode Speed & Compression

| Codec | Quality | Size (KB) | PSNR (dB) | Decode Time (ms) |
|-------|---------|-----------|-----------|------------------|
| **HakoNyans** | **Q=75** | **508** | **13.0**\* | **36.0** |
| JPEG | Q=90 | 168 | 34.6 | 9.0 |
| JPEG-XL | D=1.0 | 60 | 34.5 | 35.8 |

*\*Note: Low PSNR due to active CfL bug. Grayscale PSNR is >40dB. Disabling CfL restores PSNR to ~31dB.*

### 2. Optimization Impact

| Step | Technique | Impact on Speed | Status |
|------|-----------|-----------------|--------|
| 1 | AAN IDCT (Int) | Neutral (interface only) | **Partial** |
| 2 | SIMD Color | Positive (part of 36ms) | **Done** |
| 3 | Memory Layout | Positive (robustness) | **Done** |
| - | **Total** | **27.4ms -> 36.0ms** | **Mixed** |

**Analysis**:
The decode time increased slightly compared to Phase 6 (27ms) due to the overhead of 4:2:0 upsampling and CfL reconstruction logic, which were added in Phase 7a. The SIMD optimizations helped mitigate this, but full IDCT SIMD implementation is required to reach the <20ms target.

### 3. Next Steps (Phase 7c)

1.  **Fix CfL/4:2:0 Artifacts**: Debug the chroma reconstruction pipeline to restore >35dB PSNR.
2.  **Full AVX2 IDCT**: Implement the IDCT core using AVX2 intrinsics (currently scalar).
3.  **Palette Mode**: Implement Screen profile features. ✅ **DONE**

---
*Generated by Gemini CLI*