# HakoNyans 🐱 Current Task

## プロジェクト概要
高速デコード重視の次世代画像コーデック。
NyANS-P（Parallel Interleaved rANS + P-Index）を中核エントロピーエンジンに採用。

## 戦略ドキュメント

- プロダクト戦略の正本: `docs/PRODUCT_POSITIONING.md`
- `AGENTS.md` は開発運用ルール専用（戦略は書かない）

## 完了タスク

### Phase 0: プロジェクトセットアップ ✅
- [x] フォルダ構成作成
- [x] README.md
- [x] 設計ドキュメント（SPEC / ENTROPY / P-INDEX / SIMD / BENCHMARKS）
- [x] CMakeLists.txt 骨格（C++17）
- [x] 公開APIヘッダ（api.h, version.h）
- [x] claude.md（箱理論）
- [x] git init + 初回コミット

---

## 実装予定

### Phase 1: rANS 単体（N=1）✅ 完了
**目標**: encode → decode の往復テストが通る最小実装

- [x] `src/core/bitwriter.h` — ビット/バイト書き込み
- [x] `src/core/bitreader.h` — ビット/バイト読み込み
- [x] `src/entropy/nyans_p/rans_core.h` — rANS 基本操作
  - encode_symbol / decode_symbol
  - renormalize (LIFO処理、バッファ反転)
  - CDF テーブル構造
- [x] `src/entropy/nyans_p/rans_tables.h` — CDF/alias テーブル生成（RANS_TOTAL=4096 スケーリング）
- [x] `tests/test_rans_simple.cpp` — 5 往復テスト（全パス）
- [x] CMakeLists.txt 更新（テストビルド有効化）
- [x] 動作確認（10,000シンボルのランダム往復成功）

**箱理論チェック**:
- ✅ bitstream box と entropy box が独立
- ✅ スカラー実装が golden reference

---

### Phase 2: N=8 インターリーブ + ベンチマーク 🔜 ← 次ここから
**目標**: インターリーブで ILP 効果を確認、MiB/s 計測

- [x] `src/entropy/nyans_p/rans_interleaved.h` — N=8 状態管理（Phase 2: 独立ストリーム版）
- [x] トークン化: `src/entropy/nyans_p/tokenization.h`
- [x] `bench/bench_entropy.cpp` — N=1 vs N=8 スループット計測
- [x] 目標: >500 MiB/s → LUT版で達成

---

### Phase 3: AVX2 SIMD 実装 ✅ 完了
**目標**: rANS デコードを高速化 → LUT: 2.80x, AVX2: 2.48x

- [x] `src/entropy/nyans_p/rans_flat_interleaved.h` — 8状態・1本ストリーム共有
- [x] `src/entropy/nyans_p/rans_tables.h` — slot→symbol LUT (SIMDDecodeTable)
- [x] `src/simd/x86_avx2/rans_decode_avx2.h` — AVX2 gather+SIMD デコーダ
- [x] `src/simd/simd_dispatch.h` — ランタイム CPUID 検出 + HAKONYANS_FORCE_SCALAR
- [x] `bench/bench_phase3.cpp` — 4パス比較ベンチマーク
- [x] `tests/test_avx2_rans.cpp` — 4テスト全パス

**ベンチマーク結果** (Ryzen 9 9950X, -O3 -march=native):
| パス | デコード速度 | スピードアップ |
|------|-------------|---------------|
| N=1 scalar (baseline) | 185 MiB/s | 1.00x |
| N=8 flat scalar (CDF search) | 188 MiB/s | 1.02x |
| N=8 flat scalar (LUT) | **516 MiB/s** | **2.80x** ✓ |
| N=8 AVX2 (bulk) | 457 MiB/s | 2.48x |

**分析**: LUT が最大の効果。AVX2 gather は現行CPUではスカラーLUTに及ばないが、P-Index並列化やAVX-512時代の基盤として重要。
- [ ] `HAKONYANS_FORCE_SCALAR` 環境変数対応 ✅（simd_dispatch.h で実装済み）

---

### Phase 4: P-Index 並列デコード ✅ 完了
**目標**: マルチスレッドでデコード速度がコア数に比例

- [x] `src/entropy/nyans_p/pindex.h` — チェックポイント構造 + PIndexBuilder + シリアライズ
- [x] `src/platform/thread_pool.h` — シンプルなスレッドプール + HAKONYANS_THREADS 環境変数
- [x] `src/entropy/nyans_p/parallel_decode.h` — P-Index 並列デコーダ (CDF + LUT 版)
- [x] `tests/test_parallel.cpp` — 9テスト全パス
- [x] 1/2/4/8/16 スレッドスケーリングベンチ

**ベンチマーク結果** (Ryzen 9 9950X, 4M tokens):
| スレッド | デコード速度 | スケーリング |
|---------|-------------|-------------|
| 1 | 458 MiB/s | 1.00x |
| 2 | 859 MiB/s | 1.88x |
| 4 | 1533 MiB/s | 3.35x |
| 8 | 2366 MiB/s | 5.17x |
| 16 | 2528 MiB/s | 5.52x |

**分析**: 4コアまでほぼ線形スケーリング（効率83%）。8コア以降はメモリ帯域で飽和。

---

### Phase 5: コーデック統合（画像エンコード/デコード）
**目標**: .hkn ファイルの encode/decode が動く
**設計書**: [docs/PHASE5_DESIGN.md](docs/PHASE5_DESIGN.md)

#### Step 5.1: Grayscale end-to-end（最小動作確認）← 進行中（デバッグ中）
- [x] `src/codec/zigzag.h` — Zigzag scan LUT ✅
- [x] `src/codec/quant.h` — 量子化/逆量子化 + JPEG-like quant matrix ✅
- [x] `src/codec/transform_dct.h` — 8×8 DCT/IDCT（スカラー、分離可能 1D×2）✅
- [x] `src/entropy/nyans_p/tokenization_v2.h` — ZRUN 統合版（DC/AC 分離対応）✅
- [x] `src/codec/headers.h` — FileHeader(48B) + ChunkDirectory + QMAT ✅
- [x] `tests/test_phase5_components.cpp` — コンポーネント単体テスト（7/7 PASS）✅
- [x] `src/codec/encode.h` — Grayscale エンコーダ（DCT→量子化→ZRUN→rANS→.hkn）✅ (ビルド OK)
- [x] `src/codec/decode.h` — Grayscale デコーダ（.hkn→rANS→ZRUN→逆量子化→IDCT）✅ (ビルド OK)
- [x] `tests/test_codec_gray.cpp` — グレースケール往復テスト ✅ (作成)
- [x] デバッグ完了：全テスト PASS ✅
  - CDF 周波数アンダーフロー修正（rans_tables.h）
  - アルファベットサイズ 256→76（encode.h/decode.h）
  - AC EOB 欠落修正：全係数が非ゼロ時に ZRUN_63 未付加（tokenization_v2.h）
  - IDCT 丸め修正：負の値で誤差（transform_dct.h）
- [x] PSNR 計測：Q75=46.1dB, Q90=47.7dB, Q100=49.0dB ✅

#### Step 5.2: Color（YCbCr 4:4:4）✅ 完了
- [x] `src/codec/colorspace.h` — RGB ↔ YCbCr 整数近似 ✅
- [x] 3チャンネル独立エンコード/デコード ✅
- [x] `tests/test_codec_color.cpp` — カラー往復テスト（PSNR 39.4dB）✅
- [x] PPM の簡易読み書き（外部ライブラリなし）✅

#### Step 5.3: DC DPCM + P-Index 統合 ✅ 完了
- [x] DC 係数チャンク内 DPCM（チャンク境界でリセット）✅
- [x] Phase 4 の P-Index を codec に統合 ✅
- [x] タイル分割（大画像対応）✅
- [x] マルチスレッドデコード統合テスト ✅

#### Step 5.4: CLI + ベンチマーク ✅ 完了
- [x] `tools/hakonyans_cli.cpp` — `hakonyans encode/decode/info` ✅
- [x] `bench/bench_decode.cpp` — Full HD end-to-end（232 MiB/s）✅
- [ ] libjpeg-turbo との速度比較（同 quality）← Phase 6
- [ ] PSNR vs bpp カーブ（quality 1-100）← Phase 6

---

### Phase 6: ベンチマーク対決 ✅ 完了
**目標**: libjpeg-turbo / libjxl / libavif との比較

- [x] `bench/bench_compare.cpp` — 各ライブラリとの速度比較 ✅
- [x] Full HD / 4K テスト画像セット ✅
- [x] 圧縮率 vs デコード速度のトレードオフグラフ ✅
- [x] PSNR vs bpp カーブ（quality 1-100） ✅
- [x] BENCHMARKS.md 更新 ✅

**結果**: HakoNyans は JPEG-XL より 1.23x 高速、AVIF より 5.5x 高速。品質は 41.3 dB で JPEG の 34.6 dB を上回る。

---

### Phase 7a: 圧縮率改善（4ステップ） ✅ 完了
**目標**: ファイルサイズ 1004 KB → 490 KB 達成（6x → 2.9x JPEG 比）

- [x] **Step 1: 適応量子化（D提案）** — ブロック複雑度に応じた qstep 調整 ✅
- [x] **Step 2: 4:2:0 サブサンプリング（E提案）** — クロマダウンサンプル + アップスケール ✅
- [x] **Step 3: CfL（A提案）** — 輝度からの色予測で残差圧縮 ✅
- [x] **Step 4: Band-group CDF（F提案）** — 周波数帯域別 CDF で適応符号化 ✅

**圧縮効果** (Full HD 1920×1080, lena.ppm):
| フェーズ | ファイルサイズ | 削減量 | PSNR (Gray) | PSNR (Color) |
|---------|-------------|-------|------------|------------|
| Phase 6 (baseline) | 1004 KB | - | 40.8 dB | 39.4 dB |
| +AQ (Step 1) | 870 KB | -13% | 40.6 dB | 39.2 dB |
| +4:2:0 (Step 2) | 580 KB | -42% | 40.3 dB | 38.8 dB |
| +CfL (Step 3) | 530 KB | -47% | 40.3 dB | 38.5 dB |
| +Band CDF (Step 4) | **484 KB** | **-52%** | **40.3 dB** | **23.5 dB** |

**分析**:
- ✅ ファイルサイズ目標達成（490 KB 目標、実績 484 KB）
- ✅ グレースケール品質維持（40.3 dB）
- ⚠️ カラー品質課題（23.5 dB、原因: CfL 実装の改善が必要）
- ✅ 総削減率 52%（当初 6x → 現在 2.9x JPEG 比）

---

### Phase 7b: デコード速度改善 ✅ 完了
**目標**: 36 ms → 29 ms（Full HD）

- [x] **AVX2 色変換 int16 オーバーフロー修正** — 係数を shift-8→shift-7 に変更（PSNR 13dB→42.6dB）
- [x] **固定小数点 IDCT** — cos() 呼び出しを事前計算テーブルに置換（36ms→29.2ms, -19%）
- [x] **メモリレイアウト最適化** — タイルベース処理（Gemini実装）
- [x] テスト: 全 11/11 PASS

**デコード速度**: 36ms → **29.2ms (-19%)**、203 MiB/s

---

### Phase 7c: さらなる最適化 ✅ 完了
**目標**: AAN butterfly IDCT + Screen Profile

- [x] **Task 1: AAN butterfly IDCT** — 整数 butterfly（Loeffler/AAN 系）に差し替え
- [x] **Task 1.5: デコード並列度調整** — `std::async` のワーカー上限を 8 に制限
- [x] **Task 1 結果** — Full HD decode: **27.8ms → 19.5ms**, **305 MiB/s**, 全 11/11 テスト PASS
- [x] **Task 2: Screen Profile（Palette + 2D Copy）** — スクショ特化圧縮 ✅ 完了
  - Palette モード: 8色以下のブロック（頻度ベース抽出、Delta符号化）
  - 2D Copy モード (IntraBC): 繰り返しパターン検出（SAD探索、±64ブロック範囲）
  - 自動モード選択: Copy → Palette → DCT の優先順位
  - ファイルフォーマット v2 対応
  - 全 15/15 テスト PASS
- [x] **Task 3: ベンチマーク実施** — Screen Profile の圧縮効果を実測 ✅ 完了
  - UI Screenshots: **-52.1%** サイズ削減（browser -56.9%, vscode -52.2%）
  - Game Screens: 混合結果（-8.9% ～ +85.7%）
  - Photos: 非推奨（+36.9% 平均増加）
  - ベンチマークツール実装: `bench/bench_screen_profile.cpp`
  - 比較画像生成: `bench_results/screen_profile_compare/` (30ファイル、63MB)
- [x] ドキュメント更新（Phase 7c 完全版 + ベンチマーク結果）

---

### Phase 8: ロスレス圧縮モード ✅ 完了
**目標**: PNG代替となる完全可逆圧縮モード

- [x] **YCoCg-R 可逆変換** — 整数演算、+0.5dB coding gain
- [x] **差分フィルタ 5種** — PNG互換（None/Sub/Up/Average/Paeth）
- [x] **タイル独立並列** — 256×256 タイルで完全並列デコード
- [x] **ZigZag変換** — signed → unsigned 変換
- [x] **MAGC+REM統合** — 既存トークン化を残差に適用
- [x] **エンコーダ実装** — `encode_lossless()` / `encode_color_lossless()`
- [x] **デコーダ実装** — `decode_lossless()` / `decode_color_lossless()`
- [x] **テスト Round 1** — YCoCg-R + フィルタ基盤（7/7 PASS）
- [x] **テスト Round 2** — コーデック完全一致（7/7 PASS）
- [x] **ベンチマーク** — UI画像 84-94% 圧縮達成

**実装内容**:
- `src/codec/lossless_filter.h` (新規) — 190行
- `src/codec/colorspace.h` — YCoCg-R 追加
- `tests/test_lossless_round1.cpp` (新規) — 303行
- `tests/test_lossless_round2.cpp` (新規) — 249行
- `bench/bench_lossless.cpp` (新規) — 195行

**テスト結果**: 17/17 全テスト PASS
**実装者**: Claude Opus 4.6

---

### Phase 8b: PNG対決ベンチマーク ✅ 完了
**目標**: HakoNyans Lossless の実力を PNG と直接比較

- [x] **ベンチマークツール作成** — `bench/bench_png_compare.cpp`
  - libpng でエンコード/デコード
  - png_wrapper.h / ppm_loader.h 実装
  - 比較項目：ファイルサイズ、エンコード時間、デコード時間
- [x] **テスト画像準備** — カテゴリ別に分類（15枚）
  - UI Screenshots（3枚）
  - Natural Photos（4枚、Kodak dataset）
  - Anime（4枚、5K高解像度含む）
  - Game（2枚）
  - Synthetic（2枚、合成画像）
- [x] **ベンチマーク実行** — 全15枚で測定完了
- [x] **結果分析** — **深刻なバグ発見**
  - UI/Anime/Game: PNG圧勝（15-120倍差）
  - Photo: HKN勝利（-8~-10%）
  - 原因: CDFヘッダー or タイルサイズ問題
- [x] **ドキュメント更新** — `docs/BENCHMARKS.md` に結果追加

**結果**: Phase 8 Lossless にバグ発見 → Phase 8c で修正

---

### Phase 8c: Lossless バグ修正 ✅ 完了
**目標**: 150KB固定サイズ問題を修正 + Phase 8c リグレッション修正

#### Phase 8c-v1: Screen Profile統合（失敗）
- [x] Screen Profile統合 + 静的CDF実装
- [x] テスト: 17/17 PASS
- [x] ベンチ結果: **2-7倍悪化**（Solid 11.6KB→23.4KB、UI 35.4KB→87.2KB）
- [x] 問題発見: 均一CDF、行分割フィルタ、判定順ミス

#### Phase 8c-v2: リグレッション修正（成功）✅
- [x] **修正1: データ適応CDF復活** — 均一静的CDF削除、動的CDF復活
  - rANS圧縮が無効化されていた問題を解決
  - 残差は0中心の非均一分布 → データ適応CDFが必須
- [x] **修正2: フルイメージフィルタ** — ブロック行分割→フル予測コンテキスト
  - Palette/Copy画素をアンカーとして使用
  - 行間相関を8行で切断していた問題を解決
- [x] **修正3: 判定順変更** — Copy→Palette→Filter
  - Solid画像でCopy（4B/block）を優先使用
  - Palette（~9B/block）のオーバーヘッド削減
- [x] **テスト実行** — 17/17 PASS、bit-exact ラウンドトリップ ✅
- [x] **ベンチマーク** — UI/Gradientで改善、Solidのみ微増

**最終結果**:
- UI: 35.4KB → **30.9KB** (-12.7% ✅)
- Gradient: 33.8KB → **32.2KB** (-4.7% ✅)
- Solid: 11.6KB → **15.2KB** (+31% ⚠️ Copyオーバーヘッド)

**次**: Phase 8d（PNG再ベンチマーク）準備完了

---

### Phase 8d: PNG再ベンチマーク ✅ 完了 (2026-02-11)
**目標**: Copy/Palette stream最適化後の最終検証

- [x] **Copy codec強化** — 動的0/1/2-bit符号化（mode=2）
- [x] **Palette stream v2** — 単色省略 + 2色マスク辞書
- [x] **PNG再ベンチマーク** — 全カテゴリで劇的改善確認
- [x] **ドキュメント更新** — BENCHMARKS.md, DEVELOPMENT_HISTORY.md, README.md

**最終結果**:
- UI: **39.0x → 3.20x**（-91.8%、browser 2.15x 🏆）
- Anime: **41.5x → 4.02x**（-90.3%）
- Game: **43.1x → 3.90x**（-90.9%）
- Photo: **0.93x → 0.72x**（PNG比28%削減 ✅）

---

### Phase 8e: Artoria Decode Bug Fix ✅ 完了 (2026-02-11)
**目標**: 高解像度アニメ画像のロスレスデコードバグ修正

- [x] **バグ特定** — Palette v2 辞書オーバーフロー（255個上限）
- [x] **修正実装** — `dict_overflow` チェック追加、Raw mode フォールバック
- [x] **検証** — Artoria/Nitocris 両方で PSNR = INF 確認
- [x] **回帰テスト** — 17/17 PASS
- [x] **bench_results 統一** — 出力先を `bench_results/` に統一

**修正結果**:
- Artoria (5120×3157): **7.5dB → INF**（完全修正 🎉）
- Nitocris (5120×3157): **INF → INF**（回帰なし ✅）

**Phase 8 完全達成** 🎉🎉🎉

---

## Phase 9候補（進行中）

### Phase 9a: Lossless Auto Profile（設計）✅ 完了
**目標**: 画像タイプに応じて Copy/Palette/Filter を自動最適化し、UIワーストケースを改善

- [x] 設計ドキュメント作成 — `docs/PHASE9_LOSSLESS_PROFILE_PLAN.md`
- [x] 特徴量定義（U/T/gX/gY/var/hash/CopyHit）
- [x] 画像プロファイル判定ルール（EditorUI/BrowserUI/FlatUI/PixelArt/Anime/Photo）
- [x] Block判定しきい値（Copy→Palette→Filter）を数値で確定
- [x] P0/P1/P2 ロードマップ整理
- [x] 検証プロトコル（Ablation + 成功基準 + フォールバック）

### Phase 9b: Lossless Auto Profile（実装）🚧 未着手
**目標**: P0を encoder-only で導入し、互換を維持したまま圧縮率を底上げ

- [ ] 画像プロファイル判定を `encode_plane_lossless()` 前段に追加
- [ ] P0決定木（Copy/Palette/Filter）を実装
- [ ] Copy辞書（`hash -> last_pos`）導入
- [ ] タイル単位フォールバック（optimized vs baseline）実装
- [ ] ベンチ再測定（UI/Anime/Game/Photo + ablation）

### Phase 9c: 仕様拡張候補（P1/P2）🧪
- [ ] Palette index 文脈化 + RLE
- [ ] Copy辞書 multi-candidate 化（`hash -> vector<pos>`）
- [ ] block_types の 9文脈エントロピー最適化
- [ ] Tile palette dictionary / delta palette（要フォーマット拡張）

### Phase 9d: Compression Rate Strategy（設計）✅ 完了
**目標**: 圧縮率改善ロードマップを P0/P1/P2 で体系化し、RD基準で比較可能にする

- [x] 戦略ドキュメント作成 — `docs/PHASE9_COMPRESSION_STRATEGY.md`
- [x] 優先施策 A（P0/P1/P2）整理
- [x] 効果見積もり B（改善率/速度影響/リスク）整理
- [x] 実装ロードマップ C（短期/中期/互換運用）整理
- [x] 検証計画 D（RDカーブ/アブレーション/成功基準）整理

### Phase 9e: P0 圧縮改善（実装）✅ 完了 (2026-02-11)
**目標**: Phase 9 P0施策（Chroma量子化分離、Bit Accounting、Lossless Mode整理）を実装

- [x] **Chroma 量子化分離** — `src/codec/quant.h` に `base_quant_chroma[64]` 追加（JPEG Annex K準拠）
- [x] **3テーブルQMAT** — Y/Cb/Cr別量子化、`chroma_quality = quality - 12`
- [x] **Bit Accounting** — `bench/bench_bit_accounting.cpp` 実装（lossless/lossy両対応）
- [x] **Lossless Mode整理** — Copy/Palette/Filter判定を候補抽出ベースに整理
- [x] **後方互換性維持** — `num_tables==1` の旧形式デコード対応
- [x] **テスト** — 17/17 PASS ✅
- [x] **ベンチマーク確認** — UI 3.20x、Photo 0.78x（既存レンジ維持）
- [x] **コミット** — f294bd2

**検証結果**:
```
VSCode lossless 内訳（52.9KB）:
  block_types: 27.92% ← 最大
  palette:     26.00%
  copy:        21.31%
  filter_lo:    9.58%
  filter_hi:    8.67%
```

**次**: Phase 9f（P0検証・最適化、または P1準備）
**目標**: P0の低リスク施策を実装し、互換を維持したまま圧縮改善の土台を追加

- [x] **Bit Accounting 追加** — `bench/bench_bit_accounting.cpp`
  - lossless/lossy の byte 内訳（header/stream/block_types/palette/copy）を可視化
- [x] **Lossy 量子化刷新** — luma/chroma 分離量子化
  - `src/codec/quant.h`: chroma matrix + `build_quant_tables()`
  - `src/codec/encode.h`: chroma quality を luma から分離（`quality-12`）
  - `src/codec/decode.h`: `QMAT num_tables==3` 対応（旧1テーブルも互換維持）
- [x] **Lossless mode 判定基盤整理** — `encode_plane_lossless()`
  - Copy candidate / Palette candidate / Filter fallback を明確化
  - 既存圧縮率を崩さない保守的ガードで運用
- [x] **検証**
  - `ctest`: 17/17 PASS
  - `bench_png_compare`: 既存レンジ維持（UI 3.20x, Photo 0.78x）

---

### Phase 9f: Band-group CDF実装とチューニング ✅ 完了 (2026-02-11)
**目標**: AC係数をband別に分割してrANS CDFを独立化し、圧縮率を改善

- [x] **Band-group CDF実装** — `src/codec/band_groups.h` 追加
  - DC/LOW(1-15)/MID(16-35)/HIGH(36-63) の4バンドに分割
  - 各バンドで独立したCDFを使用（`encode_band_group_rans()`）
  - CMakeパラメータ化（`HAKONYANS_BAND_LOW_END`, `HAKONYANS_BAND_MID_END`）
- [x] **総当たりチューナー実装** — `tools/tune_band_groups.py`
  - 境界パラメータの全候補探索（117候補）
  - Pass1（粗い探索）+ Pass2（上位候補の精密測定）
  - チェックポイント/再開機能（`--resume`）
  - 実行時間: 951.9秒（約16分）
- [x] **最適パラメータ決定** — tuning結果
  - **推奨値**: `low=24, mid=43`
  - **baseline (15,31)** → 630193 bytes, 19.33ms
  - **best (24,43)** → 628331 bytes, 20.19ms
  - **改善**: サイズ -0.30%, decode +4.45%（許容範囲内）
- [x] **後方互換性** — `version_minor=2` でband-group CDF有効化
- [x] **検証** — 17/17 PASS ✅
- [x] **コミット** — 6358323, 9a1c0f8, a43f79a

---

### Phase 9g: P-Index密度自動最適化 ✅ 完了 (2026-02-11)
**目標**: P-Index（並列分割メタデータ）の密度を自動最適化し、メタ比率1〜2%以内に抑える

- [x] **動的P-Index間隔計算** — `calculate_pindex_interval()`
  - トークン数と実ストリームサイズから最適間隔を算出
  - 目標メタ比率: 品質に応じて1〜2%
  - 間隔クランプ: 64〜4096（8アライン）
- [x] **Band-group P-Index対応** — band AC (low/mid/high) ごとに動的P-Index生成
  - Tile v3 pindexスロットにband用blob格納
  - デコーダで並列decode対応（`decode_stream_parallel()`）
  - 小さいbandストリームは閾値でP-Index抑制
- [x] **Bit Accounting拡張** — P-Index測定追加
  - `PINDEX <bytes> (<percent>%)`
  - `pindex_cps` (checkpoint数推定) 表示
- [x] **検証結果**:
  - ctest: 17/17 PASS ✅
  - Q50: PINDEX 8476 bytes (1.33%) ✅
  - Q75: PINDEX 14356 bytes (1.61%) ✅
  - デコード速度: 約20ms維持
- [x] **コミット** — dbec46f, a7d66bf, 3bf4e08

**トレードオフ**:
- Q50総サイズ: 630193 → 638669 (+1.34%)
- メタ比率目標は達成、ただし圧縮率は微増

---

### Phase 9h: Lossless Mode決定最適化 ✅ 完了 (2026-02-11)
**目標**: Losslessモード決定を「推定ビット最小化」に強化し、Copy/Palette/Filter選択を最適化

- [x] **ビット推定関数追加** — `src/codec/encode.h`
  - `estimate_copy_bits()` — Copy モードの推定（dx/dy可変ビット符号化）
  - `estimate_palette_bits()` — Palette モードの推定（色数・マスク）
  - `estimate_filter_bits()` — Filter モードの推定（非ゼロ係数エントロピー）
- [x] **最適モード選択** — `encode_plane_lossless()`
  - 固定優先順位（Copy→Palette→Filter）を廃止
  - 推定ビット最小化による動的選択
  - PaletteCodec private API依存を排除（ローカル推定関数に置換）
- [x] **検証結果**:
  - ctest: 17/17 PASS ✅
  - ビルド成功（private API参照エラー修正済み）
  - bench_png_compare: 既存と同等（実質差分なし）
  - A/B比較: 3bf4e08 vs c141314 でサイズほぼ同等
- [x] **コミット** — c141314

**結果分析**:
- サイズ改善は現時点で確認されず（推定式が保守的）
- 次の最適化: モード選択統計の可視化と推定式の重み調整

---

### Phase 9h-2: Lossless Mode選択統計可視化 ✅ 完了 (2026-02-11)
**目標**: モード選択の統計を可視化し、推定式のチューニング指針を取得

- [x] **統計データ構造追加** — `src/codec/encode.h`
  - `LosslessModeStats` 構造体（候補数/選択数/推定bits 集計用）
  - 取得/リセットAPI（`get_lossless_mode_stats()`, `reset_lossless_mode_stats()`）

- [x] **統計集計ロジック** — `encode_plane_lossless()`
  - 各モード（Copy/Palette/Filter）の候補数をカウント
  - 推定ビット数を記録
  - 選択したモード（最小ビット）をカウント
  - モード別選択率を計算

- [x] **Bit Accounting統計表示** — `bench/bench_bit_accounting.cpp`
  - `Mode Selection Stats:` セクション追加
  - Copy/Palette/Filter の候補数・選択数・選択率を表示
  - est_gain_vs_filter（Filter比ビット削減量）を表示

- [x] **検証結果**:
  - ctest: 17/17 PASS ✅
  - UI (vscode.ppm, lossless):
    ```
    copy_candidates:    97112
    copy_selected:      90111 (92.71%)
    filter_selected:     3001 (3.09%)
    palette_selected:     4000 (4.12%)
    ```
  - Photo (nature_01.ppm, lossless):
    ```
    copy_candidates:    97112
    copy_selected:      47233 (48.59%)
    filter_selected:    36142 (37.18%)
    palette_selected:   13737 (14.15%)
    ```

- [x] **コミット** — 87df859

**分析結果**:
- **UI画像**: Copy優位（92.71%）— 繰り返しパターン多い
- **Photo画像**: Copy vs Filter 競合（48.59% vs 37.18%）— 多様な構造
- **推定式チューニング対象**: estimate_palette_bits() の係数（2色時が過小評価？）

**次ステップ**:
1. 統計を使って estimate_palette_bits() の係数（特に2色時）をチューニング
2. カテゴリ別（UI/Anime/Photo）で est_gain_vs_filter を集計して閾値を再設定

---

### Phase 9h-3: Photo限定モードバイアス適用 ✅ 完了 (2026-02-11)
**目標**: Photo系で確認された -5% 改善を取り込みつつ、UI回帰を防ぐ

- [x] **Photo判定の導入** — `is_photo_like_lossless_profile()` を追加
  - Y平面のサンプル8x8ブロックで Copy-hit 率を測定
  - `copy_hit_rate < 0.80` を Photo-like と判定
- [x] **P0バイアスを Photo-like 時のみ有効化**
  - 残差0の推定コストを優遇（0.5bit相当）
  - Copy推定に固定ペナルティ（+4bit相当）
  - Mode Inertia（同一モード継続 -2bit相当）
- [x] **適用経路**
  - `encode_lossless()` / `encode_color_lossless()` で判定
  - `encode_plane_lossless(..., use_photo_mode_bias)` に伝搬
- [x] **検証**
  - `ctest`: 17/17 PASS ✅
  - `bench_png_compare`:
    - `nature_01`: 982.0KB → 932.7KB (**-5.02%**)
    - `nature_02`: 1082.4KB → 1019.0KB (**-5.86%**)
    - UI（browser/vscode/terminal）: 既存レンジ維持（回帰なし）
  - `bench_decode`: 19.5ms → 20.2ms（+3.6%、許容範囲）
- [x] **コミット** — `bd0efa4`

**開発過程メモ**:
- グローバル適用版（Photo/UI共通）は Photo改善が出る一方で browser が悪化（21.5KB→25.4KB）し不採用。
- 行フィルタ選択のコスト関数置換（|residual|合計→推定bits）も効果が薄く不採用。
- 最終的に「Photo-like プロファイル時のみ有効化」に収束。

---

## Phase 9 P0 完了状況 🎉

```
✅ Phase 9e: Bit Accounting + Chroma量子化分離 + 3テーブルQMAT
✅ Phase 9f: Band-group CDF（-0.30% サイズ改善）
✅ Phase 9g: P-Index密度オート（メタ比率1〜2%達成）
✅ Phase 9h: Lossless Mode決定最適化（推定ビット最小化）
✅ Phase 9h-2: モード選択統計可視化（チューニング指針取得）
✅ Phase 9h-3: Photo限定モードバイアス適用（Photo -5%）

Phase 9 P0（コア4項目）+ チューニング2項目 完了！🏆
```

**次の実装候補（更新）**:
- [x] Phase 10a: CfL強化（整数固定小数点 + MSE適応判定）✅ (2026-02-11)
- [x] Phase 9j: MED predictor（Lossless Filter追加）✅ (2026-02-11)
- [x] Phase 9j-2: MED photo-onlyゲート（回帰リスク抑制）✅ (2026-02-11)
- [x] Phase 9k: Tile Match 4x4（UI/Natural改善、速度+0.81ms）✅ (2026-02-11)
- [x] Phase 9l-0: Palette v3辞書 + 診断カウンタ（dict_ref可視化）✅ (2026-02-12)
- [x] Phase 9l-1: LZ導入（copy stream優先）✅ (2026-02-12)
- [x] Phase 9l-2: LZ導入（block_types stream）✅ (2026-02-12)
- [x] Phase 9l-3: LZ導入（palette stream/index map）✅ (2026-02-12)
- [x] Phase 9l-debug: 停止バグ修正 + 計測clock安定化（steady_clock）✅ (2026-02-12)
  - 実装指示書: `docs/PHASE9L_LZ_STREAM_PRIORITY_INSTRUCTIONS.md`
- [x] Phase 9m-1: Copy stream Mode3（small-vector entropy coding）✅ (2026-02-12)
- [x] Phase 9m-2: Copy stream RLEトークン（連続ベクトル圧縮）✅ (2026-02-12)
- [x] Phase 9m-3: Copy stream mode自動選択（mode1/2/3/RLE）✅ (2026-02-12)
  - 実装指示書: `docs/PHASE9M_COPY_STREAM_ENTROPY_INSTRUCTIONS.md`
- [x] Phase 9n-1: filter_ids stream wrapper最適化（rANS/LZ自動選択）✅ (2026-02-12)
- [x] Phase 9n-2: filter_hi sparseモード（zero-mask + values）追加 ✅ (2026-02-12)
- [x] Phase 9n-3: filter stream mode自動選択（legacy/sparse/lz）✅ (2026-02-12)
  - 実装指示書: `docs/PHASE9N_FILTER_STREAM_WRAPPER_INSTRUCTIONS.md`
- [x] Phase 9o-1: filter_lo stream delta wrapper（legacy/delta/LZ自動選択）✅ (2026-02-12)
- [x] Phase 9o-2: filter_lo 行RLEトークン（短距離反復の圧縮）✅ (2026-02-12)
- [x] Phase 9o-3: filter_lo telemetry + mode選択可視化 ✅ (2026-02-12)
  - 実装指示書: `docs/PHASE9O_FILTER_LO_DELTA_INSTRUCTIONS.md`
- [x] Phase 9p-1: filter_lo row predictor mode（NONE/SUB/UP/AVG）✅ (2026-02-12)
- [x] Phase 9p-2: Photo-aware gate（predictor modeの適用制御）✅ (2026-02-12)
- [x] Phase 9p-3: filter_lo predictor telemetry + DoD検証 ✅ (2026-02-12)
  - 実装指示書: `docs/PHASE9P_FILTER_LO_ROW_PREDICTOR_INSTRUCTIONS.md`
- [x] Phase 9q-1: filter_lo context split mode（filter_id別サブストリーム）✅ (2026-02-12)
- [x] Phase 9q-2: Photo-only gate + fallback（legacyとの最小選択）✅ (2026-02-12)
- [x] Phase 9q-3: context split telemetry + DoD検証 ✅ (2026-02-12)
  - 実装指示書: `docs/PHASE9Q_FILTER_LO_CONTEXT_SPLIT_INSTRUCTIONS.md`
- [x] Phase 9r-1: tile4 stream wrapper（raw/rANS/LZ最小選択）✅ (2026-02-12)
- [x] Phase 9r-2: palette stream wrapper（lossless経路でraw/rANS/LZ最小選択の統一）✅ (2026-02-12)
- [x] Phase 9r-3: telemetry + DoD検証（UI/Anime中心）✅ (2026-02-12)
- [x] Phase 9s-1: copy stream wrapper（raw/rANS/LZ最小選択）✅ (2026-02-12)
- [x] Phase 9s-2: screen-profile v1（global palette + index map）設計/実装 ✅ (2026-02-12)
- [ ] Phase 9s-3: UI/Anime 30枚ベンチで PNG 同等ライン検証

---

### Phase 9m: Copy stream Mode3 実装結果 ✅ (2026-02-12)

**実装内容**:
- `copy stream` に mode3（RLE token）を追加し、mode0/1/2/3 の4-way最小選択を導入
- `src/codec/copy.h` に mode3 encode/decode 実装
- `bench_bit_accounting` に mode3統計（run token/平均run長/long run）を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `bench_bit_accounting`:
  - `vscode`: `copy_stream_bytes` **11271B -> 8419B (-25.3%)**
  - `anime_girl_portrait`: **24151B -> 2830B (-88.3%)**
  - `nature_01`: **11812B -> 7647B (-35.3%)**
- `nature_01` total size: **-0.45%**（悪化なし）

**結論**:
copy stream の圧縮効率は大幅改善。次ボトルネックは `filter_ids/filter_lo/filter_hi`。

---

### Phase 9n: Filter stream wrapper 実装結果 ✅ (2026-02-12)

**実装内容**:
- `filter_ids` を wrapper化し、raw/rANS/LZ の最小サイズ選択を導入
- `filter_hi` に sparseモード（zero-mask + nonzero values）を追加
- `bench_bit_accounting` に filter stream mode統計を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `vscode` total: **30790B -> 27829B (-9.6%)**
- `anime_girl_portrait` total: **15679B -> 12486B (-20.4%)**
- `nature_01` total: **927896B -> 927573B (-0.03%)**
- `bench_decode`: **300MiB/s帯を維持**

**結論**:
Filter stream 圧縮は有効。次の主要ボトルネックは `filter_lo`（特にPhoto/Anime）と `copy+palette` の残りコスト。

---

### Phase 9o: Filter lo stream delta 実装結果 ✅ (2026-02-12)

**実装内容**:
- `filter_lo` に wrapper（legacy/delta+rANS/LZ）を追加
- 画像特性に応じた mode 自動選択を導入（UI/AnimeでLZ優先、Photoはlegacy維持）
- `bench_bit_accounting` に filter_lo 診断を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `vscode` total: **27829B -> 26163B (-6.0%)**
- `anime_girl_portrait` total: **12486B -> 10350B (-17.1%)**
- `nature_01` total: **927573B -> 927573B (0.0%)**
- `bench_decode`: **307MiB/s**（維持）

**結論**:
UI/Anime には有効だが、Photoでは `filter_lo` が依然支配的。次は Photo向けに `filter_lo predictor` を追加して改善を狙う。

---

### Phase 9p: Filter lo row predictor 実装結果 ✅ (2026-02-12)

**実装内容**:
- `filter_lo` mode3（row predictor: NONE/SUB/UP/AVG）を追加
- `decode.h` のストリームポインタ進行バグを修正（SegFault解消）
- mode3テスト（roundtrip / mixed rows / malformed）を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `bench_bit_accounting nature_01`:
  - `filter_lo_mode0/1/2/3 = 3/0/0/0`（mode0選択）
  - total: **927573B（変化なし）**
- `bench_decode`: **284MiB/s**（目標100MiB/s超は維持）

**結論**:
mode3は機能実装としては完了したが、Photoでは採用されず圧縮改善は未達。次は `filter_id` 文脈分割による `filter_lo` エントロピー低減へ進む。

---

### Phase 9r: Tile4/Palette wrapper 最適化 実装結果 ✅ (2026-02-12)

**実装内容**:
- `tile4 stream` に wrapper 形式を追加（`raw / rANS / LZ` の最小選択）
- `palette stream`（lossless経路）を `raw / rANS / LZ` の最小選択に統一
- `headers.h` に `WRAPPER_MAGIC_TILE4 (0xAC)` を追加、バージョンを `0x000D` に更新
- `decode.h` に tile4 wrapper の復号対応（mode1=rANS, mode2=LZ）
- `bench_bit_accounting` に `tile4_raw_bytes`, `tile4_stream_mode0/1/2` を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `bench_bit_accounting`:
  - `vscode`: `tile4` **3504B -> 565B**（-83.9%）
  - `vscode`: `palette` **6597B -> 1324B**（-79.9%）
  - `anime_girl_portrait`: `tile4` **374B -> 290B**（-22.5%）
  - `nature_01`: `tile4` **9010B -> 5417B**（-39.9%）
- `bench_png_compare` カテゴリ平均:
  - UI: **+37% -> +5%**（大幅改善）
  - Anime: **+28% -> +27%**（微改善）
  - Photo: **-32% 維持**

**結論**:
`tile4` の生ペイロードは主要ボトルネックの1つだった。低リスクの wrapper 圧縮で UI の差分を大きく縮小できた。次は `palette stream` の lossless 経路統一（9r-2）を実施。

---

### Phase 9s-1: Copy stream wrapper 最適化 実装結果 ✅ (2026-02-12)

**実装内容**:
- `copy stream` に wrapper 形式を追加（`raw / rANS / LZ` の最小選択）
- `decode.h` を mode1=rANS / mode2=LZ の復号に対応
- `bench_bit_accounting` に `copy_wrapper_mode0/1/2` を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `bench_bit_accounting`:
  - `vscode`: `copy` **8419B -> 770B**（-90.9%）
  - `anime_girl_portrait`: `copy` **2830B -> 714B**（-74.8%）
  - `nature_01`: `copy` **7647B -> 6957B**（-9.0%）
- `bench_png_compare` カテゴリ平均:
  - UI: **+5% -> -34%**（PNG比で逆転）
  - Anime: **+27% -> +10%**（大幅改善）
  - Game: **+20% -> -11%**（PNG比で逆転）
  - Photo: **-32% 維持**
- `bench_decode`: **287 MiB/s**（目標100 MiB/s超を維持）

**結論**:
copy payload は未圧縮部分が大きく、wrapper最小選択で一気に削減できた。`UI/Game` はカテゴリ平均で PNG 比優位に入った。次は `anime_sunset` など残る高難度ケースに対して `screen-profile v1` を進める。

---

### Phase 9s-2: Screen-profile v1（global palette + index map）✅ (2026-02-12)

**実装内容**:
- 新タイル形式 `WRAPPER_MAGIC_SCREEN_INDEXED (0xAD)` を追加
  - 形式: global palette（`int16` 値）+ packed index map（raw/rANS/LZ最小選択）
- `encode_plane_lossless()` に screen-indexed 候補生成と最小選択を追加（screen-likeかつサイズ優位時のみ採用）
- `decode_plane_lossless()` に screen-indexed 復号を追加
- `bench_bit_accounting` に `screen_index` 項目を追加
- `tests/test_lossless_round2.cpp` に screen-indexed roundtrip テストを追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `bench_png_compare` カテゴリ平均:
  - UI: **-35%**（PNGより小さい）
  - Game: **-11%**（PNGより小さい）
  - Anime: **+10%**（残課題）
  - Photo: **-32%**（維持）
- `bench_decode`: **20.9ms / 283.7 MiB/s**（目標 >100 MiB/s維持）

**補足**:
- 本サンプルでは `anime_sunset` が未達（PNG比 +32%）で、`9s-3` でのしきい値調整と追加分岐（screen-profile判定強化）が必要。

---

### Phase 9j: MED Predictor 実装結果 ✅ (2026-02-11)

**実装内容**:
- `LosslessFilter` に MED (Median Edge Detector) を追加（FILTER_COUNT=6）
- Lossless エンコーダ/デコーダで MED をサポート
- 行単位の最適フィルタ選択に MED を統合

**ベンチマーク結果 (Photo/Natural)**:
- **kodim03 (Natural)**: 515 KiB → **453.5 KiB (-11.9%)** ⭐
- **nature_01 (Photo)**: 932 KiB → **926.9 KiB (-0.6%)**
- **nature_02 (Photo)**: 1019 KiB → **998.2 KiB (-2.0%)**

**結論**:
テクスチャの多い Kodak 画像（Natural）で 10% を超える劇的な改善を確認。風景写真（Photo）でも着実な改善。デコード速度への影響は軽微。

---

### Phase 9j-2: MED Photo-onlyゲート ✅ (2026-02-11)

**実装内容**:
- `encode_plane_lossless()` のフィルタ候補数を `lossless_filter_candidates(use_photo_mode_bias)` で切替
- `photo-like=true` のときのみ MED を候補に追加（6種）
- 非photoでは従来の5種（None/Sub/Up/Avg/Paeth）で選択
- `tests/test_lossless_round2.cpp` に回帰テスト `MED filter gate (photo-only)` を追加

**検証結果**:
- `ctest`: **17/17 PASS**
- `test_lossless_round2`: **8/8 PASS**（新規MEDゲート検証を含む）
- `bench_png_compare`（13枚セット）: **サイズ差分なし**（既存レンジ維持）
- `bench_decode`:
  - before: `20.3608 ms`
  - after: `20.4605 ms`（**+0.49%**, 許容範囲）

**結論**:
MEDの効果（Photo/Natural）を維持しつつ、UI/Anime側の将来回帰リスクを設計上で抑制。

---

### Phase 9i: P1実装準備（SIMD相性分析）✅ (2026-02-11)

**ChatGPT Pro精密評価結果**:

| 施策 | AVX2相性 | 実装難易度 | 期待高速化 | 推奨順 |
|-----|---------|---------|----------|-------|
| **CfL改善** | ⭐⭐⭐⭐⭐ (5/5) | **最低** | **12-16x** | **1位** 🔴 |
| **MED predictor** | ⭐⭐⭐⭐☆ (4/5) | 低〜中 | **8-12x** | **2位** 🔴 |
| **Tile Match/LZ** | ⭐⭐☆☆☆ (2/5) | 高 | **2-3x** | **3位** 🟡 |

**評価根拠**:
1. **CfL改善** (⭐⭐⭐⭐⭐):
   - 乗算・加算・clamp中心でSIMD化しやすい
   - ブロック独立で並列化も容易
   - 分岐なし、メモリアクセス規則的 → 最高ランク

2. **MED predictor** (⭐⭐⭐⭐☆):
   - min/max/absはAVX2向き
   - ただしleft依存で行内完全SIMD化が少し難しい
   - セミ並列実装で4-5x高速化期待

3. **Tile Match/LZ** (⭐⭐☆☆☆):
   - 探索が分岐・ハッシュ中心でAVX2効きにくい
   - SIMD効果はmemcmp/memcpy周辺のみ
   - スカラー実装が正解

**推奨実装順**: **CfL → MED → Tile Match/LZ**

**累積効果予測**:
```
現状（Phase 9h-3）: JPEG比 5.2x（Photo改善 -5%）

+ CfL改善 (-3〜-7%)      → ~4.9x
+ MED predictor (-5〜-15%)    → ~4.1x
+ Tile Match/LZ (-5〜-10%)    → ~3.7x

最終目標: JPEG比 3.0x に急速接近 🎯
```

**次ステップ**: Phase 9i-1（CfL改善実装指示書）作成予定

---

### Phase 9i-1: CfL適応化の調整（互換性修正 + サイズ悪化ガード）✅ 完了 (2026-02-11)
**目標**: CfL改善を取り込みつつ、後方互換性を維持し、Photo系でのサイズ悪化を防ぐ

- [x] **互換性修正（デコーダ）** — `parse_cfl_stream()` を追加
  - legacy形式（`nb*2 bytes`）と adaptive形式（mask+params）の両対応
  - 復号式を `legacy(a*y+b)` / `centered(a*(y-128)+b)` で切り替え
- [x] **互換性維持（エンコーダ）**
  - CfL payload は wire互換を優先して legacy形式を出力
  - `header.flags` の CfLビットは実payloadあり時のみ立てる
- [x] **サイズ悪化ガード（エンコーダ）**
  - Chromaタイルを `CfLあり/なし` の両方で試算し、小さい方を採用
  - Photo系で CfL が効かないケースを自動回避
- [x] **検証**
  - `ctest`: 17/17 PASS ✅
  - 旧エンコーダ生成 `.hkn` の復号互換: old/new decoder で md5一致 ✅
  - 新エンコーダ生成 `.hkn` の復号互換: old/new decoder で md5一致 ✅
  - `nature_01` Q50: CfL on/off とも 626,731 bytes（悪化なし）
  - `vscode` Q50: CfL on 366,646 bytes / off 410,145 bytes（改善維持）
  - `bench_decode`: 19.237ms（20ms帯維持）

**メモ**:
- adaptive CfL payload は将来拡張として decode対応を残しつつ、現行は互換優先で legacy payload を採用。
- 実運用上の安全性を優先して「試算して小さい方採用」をデフォルト化。

---

### Paper向け課題メモ（2026-02-11）

**観測された主要ボトルネック**:
- Photoカテゴリで decode が重い
  - PNG decode 平均: **17.9 ms**
  - HKN decode 平均: **45.4 ms**
  - 速度面では PNG に劣後

**論文反映済み事項**:
- 定量表記は `DecSpeedup` ではなく `Dec(ms)` に統一
- `paper/paper_ja.tex` の考察に Photo decode 課題を明記

**次の実装優先度（decode改善）**:
1. Photo向け CfL gate の厳格化（不要適用を抑制）
2. inverse DCT + dequant の AVX2 最適化
3. token decode hot path の分岐削減（プロファイル駆動）

---

### 直近実行セット: Beyond PNG（Photo filter_lo context最適化ルート）🚧

**ゴール（投稿判定ライン）**:
- `Lossless vs PNG`:
  - UI/Anime: `PNG_bytes / HKN_bytes` を段階的に縮小（現状2.8x〜4.0x帯 → まず2.2x以下）
  - Photo: `PNG_bytes / HKN_bytes` の中央値を **1.0x以上**（同等以上）
  - Decode: Photoカテゴリ平均 `Dec(ms)` を **30ms台前半以下**へ短縮（別トラック）
- `Lossy（高画質）`:
  - 目視破綻なし（アニメ肌/輪郭/UI文字の色ズレなし）
  - 同一PSNR/SSIM帯でサイズを継続改善（CfL/量子化/帯域CDFの再調整）
- `Paper readiness`:
  - 3カテゴリ（Anime/UI/Photo）の図版と表を再現スクリプトで自動生成
  - 実験条件（CPU/threads/コマンド）を固定して再現可能にする

**実行タスク（順序固定）**:
1. [x] Phase 9l-1/2/3: tile-local LZ導入（copy/block_types/palette）✅
2. [x] Phase 9l-debug: block_types Mode1 symbol-range bug修正、anime timeout解消 ✅
3. [x] Phase 9m-1/2/3: `copy stream` mode3 + RLE + 自動選択 ✅
4. [x] Phase 9n-1/2/3: `filter_ids/filter_hi` wrapper最適化 ✅
5. [x] Phase 9o-1/2/3: `filter_lo` delta/LZ最適化 ✅
6. [x] Phase 9p-1/2/3: `filter_lo` row predictor + gate + telemetry ✅
7. [x] Phase 9q-1: `filter_lo` context split mode（filter_id別サブストリーム）✅
8. [x] Phase 9q-2: Photo-only gate + fallback（legacy最小選択）✅
9. [x] Phase 9q-3: telemetry + DoD検証（Photo中心）✅
10. [x] Phase 9r-1: `tile4 stream` wrapper（raw/rANS/LZ最小選択）✅
11. [x] Phase 9r-2: `palette stream` wrapper（lossless経路でraw/rANS/LZ最小選択）✅
12. [x] Phase 9r-3: telemetry + DoD検証（UI/Anime中心）✅
13. [x] Phase 9s-1: `copy stream` wrapper（raw/rANS/LZ最小選択）✅
14. [x] Phase 9s-2: `screen-profile v1`（global palette + index map）✅
15. [ ] Phase 9s-3: UI/Anime 30枚の再計測とフォールバック条件確定
   - 指示書: `docs/PHASE9S3_UI_ANIME_GATING_INSTRUCTIONS.md`
   - 実装スコープ:
     - `screen-profile v1` の採用しきい値を UI/Anime 向けに再調整
     - タイル単位 fallback（`screen-indexed` vs `legacy lossless`）を追加
     - `anime_sunset` など未達ケースを優先して mode 採用ログを追加
   - 受け入れ基準:
     - UIカテゴリ平均 `PNG_bytes / HKN_bytes <= 1.20`
     - Animeカテゴリ平均 `PNG_bytes / HKN_bytes <= 1.05`
     - Photoカテゴリは悪化禁止（中央値 `<= +1%`）
     - `ctest` 全PASS、`bench_decode` スループット -5%以内
16. [x] Phase 9t-1: Filter-block診断カウンタ追加（色数/遷移/分散/代替palette8可能性）✅ (2026-02-12)
   - 実装スコープ:
     - `LosslessModeDebugStats` に filter選択ブロック専用統計を追加
     - `bench_bit_accounting` に `Filter block diagnostics` セクションを追加
     - `anime_sunset` / `vscode` / `nature_01` で比較ログを保存
   - 検証結果:
     - `ctest` **17/17 PASS**
     - `anime_sunset`: `palette8_better_than_filter=0/187 (0.00%)`
     - `vscode`: `palette8_better_than_filter=0/1 (0.00%)`
     - `nature_01`: `palette8_better_than_filter=0/19530 (0.00%)`
17. [x] Phase 9t-2: Anime向け Palette rescue（filter候補ブロック限定）✅ (2026-02-12, 効果未達)
   - 実装:
     - UI/ANIME向けに `palette_rescue` バイアスを追加（`unique<=8`, `transitions<=32`, `variance_proxy>=30000`）
     - 通常palette候補の遷移推定を「画素値遷移」→「index遷移」に修正
   - 結果:
     - `anime_sunset` / `vscode` / `nature_01` とも total bytes 変化なし
     - 主因: chroma平面の `palette_range_ok` 制約（`[-128,127]`）で rescue対象がほぼ発生しない
18. [x] Phase 9t-3: filter_lo mode4 gate再調整（Anime限定）✅ (2026-02-12, 効果未達)
   - 実装:
     - `filter_lo` mode3/4 探索を `PHOTO` 限定から `ANIME` へ拡張
   - 結果:
     - `anime_sunset` で `filter_lo_mode0/1/2/3/4 = 3/0/0/0/0`（mode切替なし）
     - total bytes 変化なし
19. [ ] Photo decodeのホットパス計測（`perf` / 自前timer）と上位3ボトルネック確定
20. [ ] Photo向け decode最適化（CfL gate強化 → IDCT+dequant AVX2 → token分岐削減）
21. [ ] Lossy画質回帰チェック（Artoria/UI/自然画像の目視 + PSNR/SSIM）
22. [ ] Paper用テーブル更新（`Dec(ms)`統一、サイズ・画質・速度を同一セットで再生成）
23. [ ] 投稿判定レビュー（勝ち筋/弱点/今後課題を1ページに要約）
24. [x] Phase 9u-1: Palette値域拡張（signed 16-bit / stream v4=0x42）✅ (2026-02-12)
   - 実装:
     - `src/codec/palette.h`: `Palette.colors` を `int16_t[8]` 化し、stream v4 (`0x42`) を追加
     - `src/codec/decode.h`: lossless palette 復元の `-128` 二重適用を解消、lossy palette 復元で `+128` を明示
     - `src/codec/encode.h`: palette stream診断パーサを v4 対応、lossless判定から `palette_range_ok` 制約を撤廃
     - `src/codec/headers.h`: `VERSION=0x000F`, `VERSION_PALETTE_WIDE` 追加
   - 検証:
     - `ctest` **17/17 PASS**
     - `anime_sunset` lossless total: **14,035B -> 13,731B**（-2.17%）
     - `vscode` lossless total: **4,881B**（維持）
   - `nature_01` lossless total: **817,303B**（改善）
25. [x] Phase 9u-2: filter_lo Mode5 shared/static CDF化（v0x0011）✅ (2026-02-12)
   - 実装:
     - `src/codec/shared_cdf.h` を追加し、Mode5専用の共有CDF頻度モデルを導入
     - `encode/decode` に `encode_byte_stream_shared_lz` / `decode_byte_stream_shared_lz` を追加
     - `lo_mode=5` は `v0x0010`（旧: per-tile CDF）と `v0x0011+`（新: shared CDF）を両対応
   - 検証:
     - `ctest` **17/17 PASS**
     - `anime_sunset` で `filter_lo_mode5` 採用を確認（微改善）
26. [x] Phase 9u-3: screen-indexed 競合選択の再設計（Natural対策）✅ (2026-02-12)
   - 実装:
     - `PHOTO` の事前除外を廃止し、`legacy` と `screen-indexed` を常に競合
     - 事前ゲートは `small tile` と安全上限（palette_count/bits）に限定
     - 採用条件を profile 別に統一:
       - UI: `screen <= legacy * 0.995`
       - ANIME: `screen <= legacy * 0.990`
       - PHOTO: `screen <= legacy * 1.000`（非悪化なら採用）
     - `screen` 競合の内訳テレメトリを追加（small/build/palette/bits/compete ratio）
   - 目的:
     - Natural での「候補未評価」を減らし、タイル単位で非悪化フォールバックを保証
27. [x] Phase 9u-4: screen-indexed 事前テクスチャ判定（無駄試行削減）✅ (2026-02-12)
   - 実装:
     - `analyze_screen_indexed_preflight()` を追加（`unique_sample` + `avg_run`）
     - `screen-indexed` 生成前に prefilter 判定を実施
     - `screen_rejected_prefilter_texture` など診断カウンタを追加
     - `bench_bit_accounting` に prefilter 指標を表示
   - 目的:
     - Natural/Photoでの不要な `screen-indexed` 試行を減らし、探索コストを削減

**Phase 9t 所見**:
- `Palette` が 8bit値 (`[-128,127]`) 制約のため、YCoCg chroma 平面で palette rescue がほぼ適用不能。
- 9t-2/9t-3 は実装済みだが、`anime_sunset` の total改善には繋がらなかった。
- 次に効くのは `palette` の値域拡張（9bit/符号付き）または chroma専用パスの追加。

**Phase 9u 所見**:
- 値域制約の撤廃で、chromaを含む palette 候補の探索が可能になった。
- `Mode5(shared CDF)` で mode5実採用が確認され、候補評価の土台は整った。
- `screen-indexed` は profile を問わず競合評価するよう更新。次は Natural の実データでゲート係数を再調整する。
- 改善幅は `anime_sunset` で小〜中に留まるため、`palette index map` の符号化強化は継続課題。

**受け入れ基準（DoD）**:
- [ ] `ctest` 全PASS維持
- [ ] Lossless UI中央値 `PNG_bytes / HKN_bytes <= 2.2`
- [ ] Lossless Anime中央値 `PNG_bytes / HKN_bytes <= 3.2`
- [ ] Lossless Photo中央値 `PNG_bytes / HKN_bytes <= 1.0`
- [ ] Photo decode平均 `Dec(ms)` を現状比で有意改善
- [ ] CfL色バグ再発なし（目視 + 回帰サンプル）
- [ ] `paper/` の図表がワンコマンド再生成可能

## Refactor Plan (2026-02-12)

目的:
- `encode.h` / `decode.h` の機能分離を進め、壊れにくい構造へ整理する
- 仕様追加時の差分範囲を狭め、デバッグ速度を上げる

今回の実施順:
1. [x] `lossless_block_types_codec.h` へ block-types の encode/decode を移設
2. [x] `lossless_profile_classifier.h` へ lossless profile 判定を移設
3. [x] `lossless_palette_diagnostics.h` へ palette 診断集計を移設
4. [x] `decode.h` の Natural row wrapper 復号分岐を helper 化
5. [x] `ctest --output-on-failure` で回帰確認

完了条件:
- [x] 既存 17 テスト PASS 維持
- [x] `encode.h`/`decode.h` の責務が helper 単位で追跡可能

---

## 2026-02-12 再開ハンドオフ（Phase 9w 準備）

### いまの到達点（再起動前の確定状態）
- Lossless ルートは `legacy / screen-indexed / natural-row` の競合基盤まで実装済み。
- `encode.h` / `decode.h` の大型分割は完了。
  - `src/codec/encode.h`: 806 lines
  - `src/codec/decode.h`: 725 lines
  - 新規: `src/codec/lossless_block_classifier.h`
  - 新規: `src/codec/lossless_plane_decode_core.h`
- 回帰確認: `ctest` 17/17 PASS。

### 次の最優先（Phase 9w）
1. Natural 専用の「全体連結LZ」ルート追加
2. エンコード前の判定を固定（`screen-like` / `natural-like`）
3. 評価軸を固定してA/Bを毎回同条件で比較

### 固定評価セット（6枚）
- `test_images/photo/kodim01.ppm`
- `test_images/photo/kodim02.ppm`
- `test_images/photo/kodim03.ppm`
- `test_images/natural/hd_01.ppm`
- `test_images/photo/nature_01.ppm`
- `test_images/photo/nature_02.ppm`

### 必須ログ（毎回）
- `size_bytes`（HKN / PNG）
- `Dec(ms)`
- `natural_row_selected`（tile数、採用率）
- `gain_bytes` / `loss_bytes`（route競合の勝敗内訳）

### 再開クイックスタート
```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless
./build/bench_png_compare
```

### 参照ドキュメント
- 詳細指示書: `docs/PHASE9W_NATURAL_GLOBAL_LZ_INSTRUCTIONS.md`
- 直前分析: `docs/PHASE9U_NATURAL_PNG_ANALYSIS.md`

---

## 2026-02-14 追撃順（single-core最優先）

固定ゲート（毎回）:
- [ ] `total HKN bytes <= 2,977,544`
- [ ] `median PNG/HKN >= 0.2610`
- [ ] `ctest` 17/17 PASS
- [ ] `HAKONYANS_THREADS=1 taskset -c 0` の fixed6 で `Enc(ms)` を主判定

実装順（この順で実施）:
1. [x] `plane_block_class` スカラー最適化（`estimate_filter_bits` のLUT化、挙動不変）
   - 実装: `src/codec/lossless_mode_select.h`
   - 計測:
     - single-core: `bench_results/phase9w_singlecore_blockclass_step2_scalar_final_vs_afterfix_20260214_runs3.csv`
       - `Enc(ms) 190.005 -> 176.646`
       - `plane_block_class(ms) 39.790 -> 27.356`
     - size/ratio維持: `total=2,977,544`, `median PNG/HKN=0.261035`
2. [x] `plane_block_class` AVX2 fast-path（内側ブロック限定）→ no-goでrevert
   - 記録: `docs/archive/2026-02-14_blockclass_avx2_inner_nogo.md`
3. [x] `plane_block_class` AVX512 fast-path（`HKN_EXPERIMENTAL_AVX512=1` opt-in）→ no-goでrevert
   - 記録: `docs/archive/2026-02-14_blockclass_avx512_optin_nogo.md`
4. [x] 単芯/複芯でA/B計測し、採否を確定
5. [x] no-go は `docs/archive/` に必ず保存して次へ進む
6. [x] `plane_lo_stream` mode3 分岐削減（予測評価/残差生成）→ no-goでrevert
   - 記録: `docs/archive/2026-02-14_lostream_mode3_branchcut_nogo.md`
   - 計測: `bench_results/phase9w_singlecore_lostream_mode3_branchcut_vs_step2_20260214_runs3.csv`
7. [x] `byte_stream_encoder` アロケ/pack最適化 → no-goでrevert
   - 記録: `docs/archive/2026-02-14_bstream_allocopt_nogo.md`
   - 計測:
     - `bench_results/phase9w_singlecore_bstream_allocopt_vs_step2_20260214_runs3.csv`
     - `bench_results/phase9w_singlecore_bstream_allocopt_vs_step2_20260214_runs3_rerun.csv`
8. [x] `route_natural` cost-loop fast-abs化 → no-goでrevert
   - 記録: `docs/archive/2026-02-14_routecost_fastabs_nogo.md`
   - 計測: `bench_results/phase9w_singlecore_routecost_fastabs_vs_step2_20260214_runs3.csv`
9. [x] `TileLZ::compress`（head初期化削減 + literal flush memcpy化）→ keep
   - 実装: `src/codec/lz_tile.h`
   - 計測:
     - single-core:
       - `bench_results/phase9w_singlecore_tilelz_compress_fast_vs_step2_20260214_runs3.csv`
       - `bench_results/phase9w_singlecore_tilelz_compress_fast_vs_step2_20260214_runs3_rerun.csv`
       - `bench_results/phase9w_singlecore_tilelz_compress_fast_vs_step2_20260214_runs3_rerun2.csv`
       - `bench_results/phase9w_singlecore_tilelz_compress_fast_vs_step2_20260214_runs3_rerun3.csv`
       - baseline `Enc(ms)=176.646` に対して rerun中央値は `174.960 / 175.056 / 175.765`（3/4で改善）
     - multicore:
       - `bench_results/phase9w_tilelz_compress_fast_vs_step2_multi_20260214_runs3.csv`
       - `bench_results/phase9w_tilelz_compress_fast_vs_step2_multi_20260214_runs3_rerun.csv`
   - size/ratio維持: `total=2,977,544`, `median PNG/HKN=0.261035`
10. [x] fastレーン整理（`fast` と `fast_nat` の運用分離）
   - 実装: `src/codec/encode.h`
     - `HKN_FAST_ROUTE_COMPETE=1` で fastレーンの luma route competition を opt-in
     - `HKN_FAST_ROUTE_COMPETE_CHROMA=1` で chroma route competition も opt-in
     - `HKN_FAST_ROUTE_COMPETE_CHROMA_CONSERVATIVE` で chroma保守ポリシー制御
     - `HKN_FAST_LZ_NICE_LENGTH` は fast route competition opt-in 時のみ適用
   - デフォルト挙動:
     - `--preset fast` は従来通り route competition OFF（最速優先）
   - 方針ドキュメント:
     - `docs/PHASE9W_SPEED_SIZE_BALANCE_POLICY.md` に `fast_nat`（env opt-in）運用を追記
11. [x] maxレーンに `mode2 lazy1`（レーン切替）を実装
   - 実装:
     - `src/codec/lossless_natural_route.h`
       - `GlobalChainLzParams::match_strategy` 追加（`0=greedy`, `1=lazy1`）
       - `compress_global_chain_lz` に 1-byte lookahead lazy を追加
       - `HKN_LZ_MATCH_STRATEGY` と per-call override を追加
     - `src/codec/encode.h`
       - preset計画に `natural_route_mode2_match_strategy_override` を追加
       - `HKN_FAST_LZ_MATCH_STRATEGY`（default `0`）
       - `HKN_MAX_LZ_MATCH_STRATEGY`（default `1`）
       - `max` は lazy1 をデフォルト化、`balanced` は非変更
   - 検証:
     - `ctest` 17/17 PASS
     - smoke:
       - greedy: `bench_results/phase9w_max_lane_match_strategy0_smoke_20260214_runs1.csv`
       - lazy1: `bench_results/phase9w_max_lane_match_strategy1_smoke_20260214_runs1.csv`
     - 傾向:
       - size: `2,977,544 -> 2,977,040`（-504 B）
       - Enc(ms): `198.207 -> 234.420`（+36.213 ms）
12. [x] maxレーンに `mode2 optparse_dp`（strategy=2）を段階導入
   - 目的:
     - greedy/lazy に加えて圧縮率優先の DP 解析器を max レーン専用で実装
     - token 形式は不変（デコーダ互換維持）
   - 実装:
     - `src/codec/encode.h`
       - `HKN_MAX_LZ_MATCH_STRATEGY` を `0..2` に拡張
     - `src/codec/lossless_natural_route.h`
       - `match_strategy=2` 分岐追加
       - `compress_global_chain_lz_optparse` 新設
       - DP失敗時の fallback（既存mode2）
     - `src/codec/lossless_mode_debug_stats.h`
       - optparse one-shot counter 追加（導入確認用）
   - 検証:
     - build + `ctest` 17/17 PASS
     - fixed6 single-core A/B:
       - `HKN_MAX_LZ_MATCH_STRATEGY=1` vs `2`
       - `--preset max --runs 3 --warmup 1`
       - baseline: `bench_results/phase9w_max_lane_match_strategy1_vs_optparse_20260214_runs3.csv`
       - candidate: `bench_results/phase9w_max_lane_match_strategy2_optparse_20260214_runs3.csv`
     - 判定:
       - size: `2,977,040 -> 2,975,045`（`-1,995 B`）
       - Enc(ms): `229.730 -> 496.291`（`+266.561 ms`）
       - Decision: `strategy=2` は maxレーン実験枠として保持（defaultは `1` 維持）
       - balanced/fast 非回帰（DP経路には未接続）
13. [x] `optparse_dp` 条件付き発動ゲートを導入（strategy=2）
   - 実装:
     - `src/codec/lossless_natural_route.h`
       - strategy2で `lazy1` 事前圧縮を実行
       - 事前判定:
         - `HKN_LZ_OPTPARSE_PROBE_SRC_MAX`（default `2MiB`）
         - `HKN_LZ_OPTPARSE_PROBE_RATIO_MIN/MAX`（default `20..120` permille）
       - 最小改善しきい値:
         - `HKN_LZ_OPTPARSE_MIN_GAIN_BYTES`（default `256B`）
       - 上記を満たす場合のみ DP 実行、かつ gain 閾値を満たす場合のみ採用
   - 検証:
     - baseline:
       - `bench_results/phase9w_max_lane_match_strategy1_after_dp_gate_20260214_runs3.csv`
     - candidate:
       - `bench_results/phase9w_max_lane_match_strategy2_dp_gate_final_20260214_runs3.csv`
     - 結果:
       - size: `2,977,040 -> 2,975,171`（`-1,869 B`）
       - Enc(ms): `230.732 -> 363.941`（`+133.209 ms`）
       - always-on DPより大幅に速度悪化を圧縮（`496.291 -> 363.941`）
14. [x] `optparse_dp` 2-passコストモデル（lazy1ヒスト由来）試行 → no-goでrevert
   - 計測:
     - `bench_results/phase9w_max_lane_match_strategy2_dp_gate_costmodel_20260214_runs3.csv`
   - 結果:
     - gain縮小（`-1,683 B`）かつ Enc 悪化
   - 判定:
     - no-go、flat-cost DPに戻す
   - 記録:
      - `docs/archive/2026-02-14_optparse_costmodel_nogo.md`
15. [x] `optparse_dp` 条件ゲートの閾値スイープ + strategy2既定値調整
   - 実装:
     - `tools/sweep_optparse_gate.py` を追加（runs=1粗探索）
     - 探索軸:
       - `HKN_LZ_OPTPARSE_PROBE_RATIO_MAX` (`80,120`)
       - `HKN_LZ_OPTPARSE_MIN_GAIN_BYTES` (`256,512,1024`)
       - `HKN_LZ_OPTPARSE_MAX_MATCHES` (`1,2,4`)
       - `HKN_LZ_OPTPARSE_LIT_MAX` (`32,64,128`)
   - 粗探索結果:
     - `bench_results/phase9w_optparse_gate_sweep_runs1_20260214.csv`
     - `bench_results/phase9w_optparse_gate_sweep_top10_20260214.txt`
   - 上位再計測（runs=3）:
     - `bench_results/phase9w_optparse_gate_top_c46_runs3_20260214.csv` ほか5本
   - 反映したstrategy2既定値:
     - `opt_probe_ratio_max_x1000=80`
     - `opt_min_gain_bytes=512`
     - `opt_max_matches=4`
     - `opt_lit_max=128`
   - 最終確認:
     - `bench_results/phase9w_optparse_report_check_r80_g512_m4_l128_runs3.csv`
     - vs strategy1 baseline:
       - size: `-1,795 B`
       - Enc(ms): `+108.317 ms`

### Phase 9x: LZCOST「安全ゲート化」+ スイープ ✅ 完了 (2026-02-14)
**目標**: lzcost の回帰を止め、勝てる行だけ採用する安全ゲートを導入

- [x] **安全ゲート実装** — `lossless_filter_rows.h`
  - BITS2 (baseline) と LZCOST (candidate) のコスト比較
  - マージン条件: `lzcost_best * 1000 <= lzcost_base * margin_permille`
  - 最小行長ゲート: 64画素以上
  - フォールバック: ゲート不成立時、元の `ENTROPY` (Photo) / `BITS2` (Anime) に戻る
- [x] **観測強化**
  - `filter_rows_lzcost_rows_considered/adopted/rejected` カウンタ追加
  - `bench_bit_accounting` / `bench_png_compare` 表示拡張
- [x] **パラメーター・スイープ**
  - `margin`: 980, 990, 995, 1000
  - `topk`: 2, 3
- [x] **検証結果** (fixed 6, preset=max, runs=3 再測定):
  - baseline (off): `2,956,913 bytes`
  - `m995, k3`: `2,955,258 bytes` (**-1,655 B**, 現行ベスト)
  - `m990, k3`: `2,955,616 bytes` (**-1,297 B**)
  - `m980, k3`: `2,956,155 bytes` (**-758 B**, roundtrip PASS)
  - `m1000, k3`: `2,956,975 bytes` (**+62 B**, 非推奨)
- [x] **デフォルト設定**
  - `lzcost_topk` を 3 に変更
  - `lzcost_margin_permille` を 995 に設定（圧縮率と安全性の両立）
- [x] **デバッグ機能の追加**
  - `HKN_FORCE_LOSSLESS_PROFILE` (0=UI, 1=ANIME, 2=PHOTO) によるプロファイル強制機能を追加。検証・テスト効率を向上。

**分析**: `ENTROPY` へのフォールバックを正しく実装したことで、`nature_01` 等の回帰を完全に阻止しつつ、`kodim03` での LZCOST 利得を抽出することに成功。

---

## 2026-02-14 現在目標（Phase 9X）

詳細は `docs/PHASE9X_CURRENT_GOALS.md` を正本とする。

優先順:
1. `hd_01` ワースト救済（中央値底上げ）
2. Photo decode 高速化（30ms目標）
3. Kodak (`kodim01-03`) 圧縮差の段階的縮小

運用ルール:
- `balanced` は常に非回帰ガード
- `max` で圧縮実験を進める
- no-go は必ず `docs/archive/` に保存

進捗（P1観測）:
- [x] `bench_bit_accounting` に `--json` / `--preset` を追加（機械可読の観測出力）
- [x] `tools/observe_lossless_hotspots.py` を追加（fixed6結果と内訳の統合レポート）
- [x] `hd_01` 観測レポートを作成: `docs/archive/2026-02-14_hd01_hotspot_report.md`
- [x] `filter_lo mode7`（context毎 legacy/shared CDF 切替）を実装・検証
  - 結果: 圧縮改善なし（`avg_shared_ctx=0.00`）、no-go記録
  - 記録: `docs/archive/2026-02-14_mode7_mixed_ctx_cdf_trial.md`
- [x] `filter_lo` モード運用基準を固定（昇格条件・no-go運用）
  - `docs/LOSSLESS_FLOW_MAP.md` / `docs/PHASE9X_CURRENT_GOALS.md`

---

## 2026-02-14 TileLZ 強化（opt-in化）結果

目的:
- `TileLZ` の hash-chain + lazy 探索を導入しつつ、`balanced` 回帰を防ぐ

実施:
- [x] `src/codec/lz_tile.h` に hash-chain/lazy 探索を追加
- [x] 旧シンプル探索を保持
- [x] env パラメータを追加
  - `HKN_TILELZ_CHAIN_DEPTH` (0..128)
  - `HKN_TILELZ_WINDOW_SIZE` (1024..65535)
  - `HKN_TILELZ_NICE_LENGTH` (3..255)
  - `HKN_TILELZ_MATCH_STRATEGY` (0=greedy, 1=lazy1)
- [x] デフォルトを `depth=0, strategy=0`（opt-in有効化方式）に設定

検証:
- [x] build + `ctest` 17/17 PASS
- [x] fixed6 (`balanced`, single-core, runs=3) A/B 計測
  - default (`depth=0, strategy=0`):
    - total bytes `2,977,418`
    - median PNG/HKN `0.2610`
    - median Enc `213.150 ms`
    - median Dec `13.080 ms`
  - opt-in (`depth=1, strategy=1`):
    - total bytes `2,977,193` (`-225B`)
    - median PNG/HKN `0.2658`
    - median Enc `222.870 ms`（`+9.720 ms`）
    - median Dec `13.119 ms`（`+0.039 ms`）

判定:
- `balanced` 既定には昇格しない（速度効率不足）
- hash-chain/lazy は `max/検証` 向け opt-in として継続

記録:
- `docs/archive/2026-02-14_tilelz_chain_optin_ab.md`

---

## 2026-02-14 ChatGPT協議結果: kodim03救済3段階計画

ChatGPTとの議論で「いいところどり」方針が確定。

### 現状の問題（再確認）
- kodim03: HKN 370KB vs PNG 120KB（3倍）
- 原因: MEDフィルタ選択（SADベース）+ LZ品質
- TileLZ強化、Mode5調整だけでは限界

### 3段階改善計画

#### 段階1: フィルタ選択をエントロピー推定ベース化（maxレーン限定）
- **目的**: SAD（絶対誤差和）から、圧縮後サイズ推定に変更
- **対象**: `lossless_filter_rows.h` のコスト計算
- **手法**:
  - 各行で各フィルタの「残差エントロピー」を推定
  - 最小エントロピーのフィルタを選択
  - PNGはPaeth 99%、HKNはMED 98% → この差を縮める
- **環境変数**: `HKN_FILTER_ROWS_COST_MODEL=entropy` (default: sad)
- **検証**: kodim03でPaeth使用率が上がるか

#### 段階2: LZ有効性プローブ（早期スキップ）
- **目的**: LZが効かない画像（kodim03等）ではmode2/5をスキップ
- **対象**: `lossless_filter_lo_codec.h`
- **手法**:
  - LZ圧縮前に「圧縮率予測」を実行
  - 予測値が閾値未満ならLZ評価をスキップ
  - エンコード速度向上も期待
- **環境変数**: `HKN_FILTER_LO_LZ_PROBE_THRESHOLD` (default: 0.8)

#### 段階3: Mode5トークン指向化（PNG方式導入）
- **目的**: LZ出力（literal/len/dist）を別々に符号化
- **対象**: `lossless_filter_lo_codec.h` Mode5実装
- **手法**:
  - 現在: LZバイナリ → rANS
  - 改善: LZトークン分解 → literal用CDF/len用CDF/dist用CDF → rANS
  - PNGの「動的ハフマン」に相当
- **互換性**: bitstream変更が必要、v0x0012以降で検討

### 実施順序と判断基準

1. **段階1を優先**: インパクト大、リスク低い
2. **段階1で改善確認できたら段階2へ**
3. **段階3は長期的**: bitstream互換性考慮

### 即座に始めるタスク

- [ ] 段階1実装準備
  - `lossless_filter_rows.h` のコストモデル調査
  - エントロピー推定関数の実装方針
  - env変数設計

---

## 2026-02-14 段階1実装結果（Entropy Stage1）

実装:
- [x] `src/codec/lossless_filter_rows.h`
  - `HKN_FILTER_ROWS_COST_MODEL=entropy` を本格化
  - Shannon entropy 推定（lo/hi ヒストグラム）を導入
  - 2段評価（BITS2 coarse -> entropy精査）を導入
  - 追加 env:
    - `HKN_FILTER_ROWS_ENTROPY_TOPK` (default: 2)
    - `HKN_FILTER_ROWS_ENTROPY_HI_WEIGHT_PERMILLE` (default: 350)
- [x] `tests/test_lossless_round2.cpp`
  - `test_filter_rows_entropy_differs_from_sad` 追加
  - `test_filter_rows_entropy_env_roundtrip` 追加

検証:
- [x] build + `ctest` 17/17 PASS
- [x] kodim03 単体（`bench_bit_accounting`）:
  - SAD total `369,844` -> entropy total `369,747`（`-97`）
  - row_hist(N/S/U/A/P/M): `0/3/5/0/19/1509` -> `0/3/13/0/24/1496`
- [x] fixed6 (`balanced`, runs=3, single-core):
  - SAD:
    - total `2,977,418`
    - median PNG/HKN `0.2610`
    - median Enc `207.242 ms`
    - median Dec `13.254 ms`
  - entropy(stage1):
    - total `2,954,069`（`-23,349`）
    - median PNG/HKN `0.2609`（微減）
    - median Enc `223.846 ms`（`+16.604 ms`）
    - median Dec `13.193 ms`（`-0.061 ms`）

判定:
- 圧縮量（total bytes）は改善。
- ただし `balanced` gate（median PNG/HKN `>= 0.2610`）を僅差で下回る。
- `balanced` 既定昇格は保留。`max` / 圧縮優先レーン候補として継続評価。

記録:
- `docs/archive/2026-02-14_filter_rows_entropy_stage1_ab.md`

---

## 2026-02-14 レーン結線 + LZプローブ導入

実装:
- [x] `src/codec/encode.h`
  - presetごとに `filter_row_cost_model` と `filter_lo_lz_probe_enable` を明示
  - `fast`: SAD + probe on
  - `balanced`: SAD + probe off
  - `max`: ENTROPY + probe on
- [x] `src/codec/lossless_filter_rows.h`
  - preset default + env override の解決ロジックを導入
- [x] `src/codec/lossless_filter_lo_codec.h`
  - LZプローブ（サンプル圧縮比判定）を導入
  - env:
    - `HKN_FILTER_LO_LZ_PROBE_MIN_RAW_BYTES`
    - `HKN_FILTER_LO_LZ_PROBE_SAMPLE_BYTES`
    - `HKN_FILTER_LO_LZ_PROBE_THRESHOLD`
    - `HKN_FILTER_LO_LZ_PROBE_THRESHOLD_PERMILLE`
- [x] ソース↔ドキュメント結線
  - `docs/LOSSLESS_FLOW_MAP.md` を追加
  - 主要実装箇所に `DOC:` コメント追加

検証:
- [x] build + `ctest` 17/17 PASS
- [x] fixed6 smoke (`runs=1`)
  - balanced total `2,977,418`（SAD維持）
  - max total `2,954,276`（圧縮寄り）
  - balanced + env entropy total `2,954,069`（env override有効確認）

記録:
- `docs/archive/2026-02-14_preset_lane_binding_and_lz_probe.md`

---

## 2026-02-14 LZ Probe観測強化 + スイープ結果

実装:
- [x] `src/codec/lossless_mode_debug_stats.h`
  - `filter_lo_lz_probe_*` カウンターを追加
- [x] `src/codec/lossless_filter_lo_codec.h`
  - probe実行時の enabled/checked/pass/skip と sample bytes を計測
- [x] `bench/bench_png_compare.cpp`
  - CSV列に `hkn_enc_lo_lz_probe_*` を追加
  - fixed6集計に probe 指標を表示
- [x] `bench/bench_bit_accounting.cpp`
  - `Filter lo diagnostics` に probe 指標を表示
- [x] `tools/sweep_filter_lo_lz_probe.py`
  - `threshold/sample/min_raw` の総当たり自動化を追加
  - gate: `total_hkn_bytes <= baseline` かつ `median(PNG/HKN) >= baseline`

検証:
- [x] build + `ctest` 17/17 PASS
- [x] fixed6 smoke
  - `balanced` (`runs=1`): probe off を確認
  - `max` (`runs=1`): probe counters がCSV/集計に出ることを確認
- [x] `preset=max` probe sweep (`runs=1`, 36組)
  - 全組み合わせで `total_hkn_bytes` 同一（`2,954,276`）
  - gateは全件PASS
  - 速度差は小さく、`runs=1` ではノイズ域
- [x] 上位候補を `runs=3` で再測
  - tuned (`th=980,sample=4096,min_raw=2048`) は baseline 比で
    - total: `±0`
    - median Enc: `+0.96 ms`（微悪化）
    - median Dec: `+0.04 ms`

判定:
- probe パラメータ最適化は現状の固定6では有意改善なし。
- 既定値据え置き（`threshold=1.03 / sample=4096 / min_raw=4096`）。
- 以降は probe 自体より、`mode2 eval` と `filter_rows` 本体最適化を優先。

記録:
- `bench_results/phase9w_filter_lo_probe_sweep_max_runs1.csv`
- `bench_results/phase9w_filter_lo_probe_sweep_top10_max_runs1.txt`

---

## 2026-02-14 ソース分割（filter_lo codec/decode）

実装:
- [x] `src/codec/lossless_filter_lo_codec.h` から mode6 / mode7+mode8 を分離
  - `src/codec/lossless_filter_lo_codec_mode6.inc`
  - `src/codec/lossless_filter_lo_codec_mode7_mode8.inc`
- [x] `src/codec/lossless_filter_lo_decode.h` から mode6 / mode7 / mode8 を分離
  - `src/codec/lossless_filter_lo_decode_mode6.inc`
  - `src/codec/lossless_filter_lo_decode_mode7.inc`
  - `src/codec/lossless_filter_lo_decode_mode8.inc`

結果:
- `lossless_filter_lo_codec.h`: 895行 → 605行
- `lossless_filter_lo_decode.h`: 914行 → 348行
- 17/17 テスト PASS（`ctest --test-dir build --output-on-failure`）

## 2026-02-14 ソース分割（encode_lossless_impl）

実装:
- [x] `src/codec/encode_lossless_impl.h` を集約ヘッダー化
- [x] API層を分離: `src/codec/encode_lossless_api_impl.h`
- [x] ルート/補助ロジックを分離: `src/codec/encode_lossless_routes_impl.h`

結果:
- `encode_lossless_impl.h`: 913行 → 4行
- 17/17 テスト PASS（`ctest --test-dir build --output-on-failure`）

## 2026-02-14 ソース分割（natural_route / lz_impl）

実装:
- [x] `src/codec/lossless_natural_route.h` を集約ヘッダー化
- [x] `src/codec/lossless_natural_route_detail_impl.inc` を追加
- [x] `src/codec/lossless_natural_route_encode_padded_impl.inc` を追加
- [x] `src/codec/lossless_natural_route_lz_impl.h` を集約ヘッダー化
- [x] `src/codec/lossless_natural_route_lz_config.inc` / `src/codec/lossless_natural_route_lz_optparse.inc` / `src/codec/lossless_natural_route_lz_compress.inc` を追加

結果:
- `lossless_natural_route.h`: 834行 → 67行
- `lossless_natural_route_lz_impl.h`: 783行 → 5行
- 17/17 テスト PASS（`ctest --test-dir build --output-on-failure`）
