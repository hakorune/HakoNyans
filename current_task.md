# HakoNyans 🐱 Current Task

## プロジェクト概要
高速デコード重視の次世代画像コーデック。
NyANS-P（Parallel Interleaved rANS + P-Index）を中核エントロピーエンジンに採用。

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

**次の実装候補**:
- [ ] Phase 9h-2: モード選択統計可視化（bench_bit_accounting拡張）
- [ ] Phase 9 P1: MED predictor / CfL / Tile Match/LZ
