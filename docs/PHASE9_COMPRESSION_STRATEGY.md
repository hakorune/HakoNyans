# Phase 9 Compression Strategy

**Date**: 2026-02-11  
**Scope**: 圧縮率改善フェーズ（Lossless + Lossy）  
**Policy**: デコード速度の優位を維持しつつ、エンコーダ側の最適化で圧縮率を伸ばす

## 前提

- HKN の `Q` と JPEG の `Q` は同一尺度ではないため、`Q値の横比較` は不正確。
- 比較は必ず `RDカーブ（同一画質でのサイズ比較）` で行う。
- P0 は互換とデコード負荷を壊さない施策を優先する。

## 実装ステータス（2026-02-11）

- ✅ `Bit Accounting` 実装済み（`bench/bench_bit_accounting.cpp`）
- ✅ `Lossy 量子化刷新 + クロマQ分離` 実装済み
  - `QMAT` は 3テーブル対応（Y/Cb/Cr）
  - デコーダは旧1テーブル形式も後方互換で読める
- ✅ `Lossless mode判定` は安全側の基盤整理まで実装済み
  - Copy / Palette / Filter の候補抽出と明示的分岐を導入
  - 既存ベンチ圧縮率レンジを維持
- ⏳ 未着手（P0残）
  - P-Index密度オート（メタ比率制御）
  - mode選択の本格 bit-estimation + ablation チューニング

## A. 施策一覧（優先度順）

### P0（互換/速度リスク最小）

1. Bit Accounting（ビット内訳可視化）
2. P-Index 密度の自動最適化（メタ比率上限 1〜2%）
3. Lossless の mode 決定を「推定ビット最小化」に強化（Copy/Palette/Filter）
4. Lossy 量子化テーブル刷新 + クロマQ分離
5. Band-group CDF 強化（帯域別/チャンネル別）

### P1（圧縮率をさらに伸ばす）

6. Lossless 可逆色変換の追加（YCoCg-R 固定から拡張）
7. Lossless predictor へ MED（JPEG-LS系）を追加
8. タイル内 match/LZ 系トークン導入（並列性を壊さない範囲）
9. Lossy CfL（Chroma from Luma）導入

### P2（差別化機能）

10. Lossy palette/intra-copy ハイブリッド（UI/アニメ向け）
11. Super-res + Restoration（低ビットレート専用プロファイル）
12. Film grain synthesis（写真系オプション）

## B. 効果見積もり（初期レンジ）

| 施策 | 優先 | 期待改善（サイズ） | 効く領域 | Encode影響 | Decode影響 | 備考 |
|---|---:|---:|---|---|---|---|
| Bit Accounting | P0 | 方針ミス回避 | 全カテゴリ | 小 | 0 | 必須土台 |
| P-Index密度オート | P0 | -1%〜-8% | 小画像/低bitrate | 小 | 0〜微減 | 並列度とトレード |
| Mode決定最適化 | P0 | -3%〜-12% | UI/ゲーム/アニメ | 中 | 0 | encoder-only可 |
| 量子化表刷新+クロマQ | P0 | -10%〜-35% | 写真/混在UI | 小〜中 | 0 | 画質設計が要点 |
| Band-group CDF | P0 | -3%〜-10% | 写真/アニメ | 小 | ほぼ0 | 低リスク |
| 可逆色変換追加 | P1 | -5%〜-20% | 写真/混在UI | 中 | 微小 | 仕様拡張寄り |
| MED predictor追加 | P1 | -5%〜-15% | 写真 | 小 | 小 | 実装軽量 |
| タイル内match/LZ | P1 | -5%〜-25% | UI/ゲーム | 中〜大 | 小〜中 | 並列性との両立要 |
| CfL | P1 | -3%〜-7% | 写真/アニメ | 中 | 小 | 色ずれ管理が必要 |
| Lossy palette/intra-copy | P2 | -10%〜-40%* | UI/アニメ | 大 | 小〜中 | *ケース依存 |

## C. 実装ロードマップ

### 短期（P0）

1. Bit Accounting を追加
2. P-Index密度オート導入
3. Lossless mode 決定を推定ビット最小化に更新
4. Lossy quant/chromaQ の再設計
5. Band-group CDF 改善

### 中期（P1）

1. 可逆色変換の tile選択導入
2. MED predictor 追加
3. CfL 導入
4. タイル内 match/LZ を追加

### 互換運用ルール

- Baseline（現行フォーマット）を常に残す
- 新機能は `feature_bits` で明示
- 非対応 feature を読んだデコーダは安全に失敗させる
- エンコーダは `--compat=baseline` を常備する

## D. 検証計画

### D-1. データセット

- UI: browser/vscode/terminal 系
- Anime: 線画 + グラデ混在
- Game: pixel-art + 3D UI混在
- Photo: Kodak系 + 実写セット

### D-2. Lossless 指標

- `ratio = PNG_bytes / HKN_bytes`（>1 で HKN 優位）
- カテゴリ平均だけでなく `p5`（ワースト側）を必須監視

### D-3. Lossy 指標

- RDカーブ: `bpp vs PSNR/SSIM/MS-SSIM`
- 比較は同一指標で揃える（Q値一致比較はしない）
- アブレーション:
  - Base
  - +Quant/ChromaQ
  - +Band CDF
  - +CfL
  - +（必要なら）4:2:0

### D-4. 成功基準（初期）

- UI:
  - median 改善（例: 3.2x -> 3.6x）
  - p5 を 2.0x 以上に維持
- Anime/Game:
  - 現状維持以上（+5%目安）
- Photo:
  - p50 で 1.0x 以上、p5 で 0.95x 以上を目標
- Decode速度:
  - P0/P1 は現状比 ±5% 以内

### D-5. 安全策

- タイル単位で baseline と optimized を両方試算し、小さい方を採用
- これにより判定ミス時の悪化を抑制する

## 参考

- WebP Lossless Compression Techniques  
  https://developers.google.com/speed/webp/docs/compression
- LOCO-I / JPEG-LS predictor-context design  
  https://www.sfu.ca/~jiel/courses/861/ref/LOCOI.pdf
- JPEG XL White Paper (modular / entropy / speed-oriented design)  
  https://ds.jpeg.org/whitepapers/jpeg-xl-whitepaper.pdf
- AV1 core tools (screen content tools overview)  
  https://www.jmvalin.ca/papers/AV1_tools.pdf
- Predicting Chroma from Luma in AV1  
  https://arxiv.org/abs/1711.03951
