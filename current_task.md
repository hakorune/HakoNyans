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

- [ ] `src/codec/headers.cpp` — FileHeader / TileTable
- [ ] `src/codec/transform_dct.cpp` — DCT 正/逆変換
- [ ] `src/codec/quant.cpp` — 量子化/逆量子化
- [ ] `src/codec/colorspace.cpp` — YCbCr ↔ RGB
- [ ] `src/codec/encode.cpp` — エンコードパイプライン
- [ ] `src/codec/decode.cpp` — デコードパイプライン
- [ ] `tools/hakonyans_cli.cpp` — CLI (encode/decode/info)
- [ ] エンドツーエンド往復テスト（画像 → .hkn → 画像）

---

### Phase 6: ベンチマーク対決
**目標**: libjpeg-turbo / libjxl / libavif との比較

- [ ] `bench/bench_decode.cpp` — エンドツーエンド計測
- [ ] Full HD / 4K テスト画像セット
- [ ] 圧縮率 vs デコード速度のトレードオフグラフ
- [ ] BENCHMARKS.md 更新

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
