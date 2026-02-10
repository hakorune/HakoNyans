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
- [x] ドキュメント更新（Phase 7c 完全版）

---

## 技術メモ

### rANS 基本操作（覚書）
```
encode: state = (state / freq) * total + (state % freq) + bias
decode: slot = state % total → symbol lookup → state update
renorm: state が下限を下回ったらバイトを読む/書く
```

### 重要な設計判断
- **C++17** 採用（テンプレート、constexpr、structured bindings）
- **AVX2 + NEON** が Tier 1、AVX-512 はボーナス
- **rANS 状態は 32-bit**（AVX2 で 8 個同時処理）
- **小アルファベット** → LUT が L1 キャッシュに乗る設計
- **REM は raw bits** → rANS の負荷を最小化
