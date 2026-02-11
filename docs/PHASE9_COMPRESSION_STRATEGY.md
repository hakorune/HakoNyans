# Phase 9 Compression Strategy

**Date**: 2026-02-11  
**Scope**: 圧縮率改善フェーズ（Lossless + Lossy）  
**Policy**: デコード速度の優位を維持しつつ、エンコーダ側の最適化で圧縮率を伸ばす

## 前提

- HKN の `Q` と JPEG の `Q` は同一尺度ではないため、`Q値の横比較` は不正確。
- 比較は必ず `RDカーブ（同一画質でのサイズ比較）` で行う。
- P0 は互換とデコード負荷を壊さない施策を優先する。

## 実装ステータス（2026-02-11）

### Phase 9 P0（互換/速度リスク最小）✅ 完了

- ✅ `Bit Accounting` 実装済み（`bench/bench_bit_accounting.cpp`）
- ✅ `Lossy 量子化刷新 + クロマQ分離` 実装済み
  - `QMAT` は 3テーブル対応（Y/Cb/Cr）
  - デコーダは旧1テーブル形式も後方互換で読める
- ✅ `Band-group CDF` 実装済み（`src/codec/band_groups.h`）
  - AC係数を DC/LOW/MID/HIGH の4バンドに分割
  - 各バンド独立CDFでrANS符号化
  - チューニング結果: `(24,43)` で -0.30% サイズ改善、decode +4.45%（許容範囲）
  - 総当たりtuner実装済み（`tools/tune_band_groups.py`）
- ✅ `P-Index密度オート` 実装済み（メタ比率制御）
  - 動的P-Index間隔計算（`calculate_pindex_interval()`）
  - Band-group P-Index対応（low/mid/high 各ストリーム）
  - メタ比率: Q50 1.33%, Q75 1.61%（目標1〜2%達成）
  - トレードオフ: Q50総サイズ +1.34%（メタ比率優先）
- ✅ `Lossless Mode決定最適化` 実装済み
  - 推定ビット最小化による動的モード選択
  - `estimate_copy_bits()`, `estimate_palette_bits()`, `estimate_filter_bits()`
  - 固定優先順位を廃止、ビット推定で最適モード選択
  - モード選択テレメトリ追加（候補数/選択率/候補平均bits 可視化）
  - Photo限定バイアス適用（copy-hit率ベース判定）
  - 実測: Photo `nature_01` -5.02%、`nature_02` -5.86%、UI回帰なし
  - デコード影響: +3.6%（20ms帯維持、許容範囲）

### Phase 9 P1（圧縮率をさらに伸ばす）🚧 進行中

- ✅ MED predictor（JPEG-LS系）追加（Phase 9j）
- ✅ CfL（Chroma from Luma）互換性修正 + サイズ悪化ガード（Phase 9i-1）
- ✅ Palette v3 辞書 + 診断カウンタ（Phase 9l-0）
- ✅ タイル内 LZ 系トークン導入（Phase 9l-1/2/3）
- ✅ 9l-debug（block_types Mode1 symbol-range bug修正 + steady_clock化）
- ✅ Copy stream Mode3 RLE entropy coding（Phase 9m）
- ⏳ 可逆色変換の拡張（YCoCg-R 固定から）
- ⏳ Filter stream entropy最適化（Phase 9n）

### Phase 9l 実装結果（2026-02-12）

- 診断結果（`bench_bit_accounting`）より、UIの主要コストは `copy / block_types / palette`。
- `palette dict_ref` は高く、色表辞書は既に効いている。
- Phase 9l で tile-local LZ（copy/block_types/palette）を導入し、UI縮小を確認。
  - 例: `vscode` の `block_types` は 7607B → 1047B
  - `bench_png_compare` UI平均は 2.82x → 2.46x まで改善（PNG比）
- 9l-debugで停止バグを修正:
  - `encode_block_types()` Mode1 を `encode_tokens(76)` から `encode_byte_stream(256)` に修正
  - `bench_png_compare` 計測clockを `steady_clock` に統一（負値表示を解消）
- 実装指示書: `docs/PHASE9L_LZ_STREAM_PRIORITY_INSTRUCTIONS.md`
- デバッグ指示書: `docs/PHASE9L_LZ_DEBUG_INSTRUCTIONS.md`

### Phase 9m 実装結果（2026-02-12）

- `copy stream` に mode3（RLE token）を追加し、mode0/1/2/3 の4-way最小選択を導入。
- `ctest`: 17/17 PASS
- `bench_bit_accounting`（lossless, 1920x1080）:
  - `vscode`: `copy_stream_bytes` 11271B -> 8419B（-25.3%）
  - `anime_girl_portrait`: 24151B -> 2830B（-88.3%）
  - `nature_01`: 11812B -> 7647B（-35.3%）
- `nature_01` total size は -0.45% 改善（悪化なし）。
- 速度面は `bench_decode` で 300MiB/s 帯を維持。

### Phase 9n 次アクション（2026-02-12）

- `copy stream` の次ボトルネックは `filter_ids/filter_lo/filter_hi`。
- 次は filter stream 側の圧縮を改善する:
  1. `filter_ids` wrapper（legacy rANS と LZ wrapper の最小選択）
  2. `filter_hi` sparseモード（zero-mask + nonzero values）
  3. tileごとの filter stream mode 自動選択（legacy/sparse/lz）
- 新規指示書: `docs/PHASE9N_FILTER_STREAM_WRAPPER_INSTRUCTIONS.md`

## 直近の開発過程（9h-2 / 9h-3 / 9i-1 / 9j / 9j-2）

### 9h-2: 観測強化
- `bench_bit_accounting` に mode telemetry を追加
  - `copy_candidates / palette_candidates / selected比率`
  - `est_copy_bits_sum / est_palette_bits_sum`（候補平均bits）
- 目的: 推定式のズレ（推定bits vs 実stream bytes）を可視化し、改善対象を特定

### 9h-3: 条件付き適用へ転換
- グローバル適用版（0.5bit化 + Copy penalty + Mode inertia）は Photo改善が大きい一方で UI回帰が発生
- そのため、Photo-like時のみ有効化するゲートを導入
  - 判定指標: Y平面サンプル8x8の exact Copy-hit率
  - しきい値: `copy_hit_rate < 0.80`
- 効果: Photoで -5%級の改善を維持しつつ、UIレンジは維持

### 9i-1: CfL調整（互換性修正 + サイズ悪化ガード）
- デコーダに `parse_cfl_stream()` を追加し、legacy/adaptive CfL payload の両方を解釈可能にした
- エンコーダは wire互換を優先し、CfL payload を legacy形式で出力
- Chromaタイルで `CfLあり/なし` を両方試算し、小さい方を採用（タイル単位フォールバック）
- 実測:
  - `nature_01` Q50: CfL on/off とも 626,731 bytes（悪化回避）
  - `vscode` Q50: CfL on 366,646 bytes / off 410,145 bytes（改善維持）
  - `bench_decode`: 19.237ms（速度帯維持）
  - `ctest`: 17/17 PASS

### 9j: MED predictor 追加（lossless filter 6種化）
- `LosslessFilter` に MED を追加（`FILTER_COUNT=6`）
- エンコーダ/デコーダ双方で MED をサポート
- 実測:
  - `kodim03`: 515.0 KiB → 453.5 KiB（-11.9%）
  - `nature_02`: 1019.0 KiB → 998.2 KiB（-2.0%）
  - `nature_01`: 932.7 KiB → 926.9 KiB（-0.6%）
- `ctest`: 17/17 PASS

### 9j-2: MED photo-onlyゲート（回帰リスク抑制）
- `encode_plane_lossless()` のフィルタ候補を `use_photo_mode_bias` で切替
  - photo-like: 6種（MED含む）
  - non-photo: 5種（PNG互換系のみ）
- 追加テスト:
  - `tests/test_lossless_round2.cpp` に `MED filter gate (photo-only)` を追加
- 実測:
  - `bench_png_compare`（13枚）でサイズ差分なし
  - `bench_decode`: 20.3608ms → 20.4605ms（+0.49%）
  - `ctest`: 17/17 PASS



## A. 施策一覧（優先度順）

### P0（互換/速度リスク最小）

1. Bit Accounting（ビット内訳可視化）
2. P-Index 密度の自動最適化（メタ比率上限 1〜2%）
3. Lossless の mode 決定を「推定ビット最小化」に強化（Copy/Palette/Filter）
4. Lossy 量子化テーブル刷新 + クロマQ分離
5. Band-group CDF 強化（帯域別/チャンネル別）

### P1（圧縮率をさらに伸ばす）

6. Filter stream wrapper最適化（filter_ids の mode最小選択）
7. Filter_hi sparseモード（zero-mask + nonzero values）
8. Filter stream mode自動選択（legacy/sparse/lz）
9. Lossless 可逆色変換の追加（YCoCg-R 固定から拡張）
10. Lossy CfL（Chroma from Luma）導入

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
| filter_ids wrapper最適化 | P1 | -1%〜-5% | UI/Anime/Photo | 小〜中 | 小 | rANS/LZの自動選択 |
| filter_hi sparseモード | P1 | -2%〜-10% | Photo/Anime | 中 | 小 | hi-byteが疎な画像で有効 |
| filter stream自動選択 | P1 | -1%〜-6% | 全カテゴリ | 小 | 小 | 悪化回避ガード |
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
