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

- [ ] `src/entropy/nyans_p/rans_interleaved.h` — N=8 状態管理
  - InterleavedRANSEncoder: ラウンドロビンで8状態に割り当て
  - InterleavedRANSDecoder: 8状態から順次復号
- [ ] トークン化（RUN / MAGC / EOB / SIGN / REM）
  - `src/entropy/nyans_p/tokenization.h`
  - RUN(0..15) / RUN_ESC / MAGC(0..11) / EOB / SIGN
  - REM は raw bits（rANS 外）
- [ ] `bench/bench_entropy.cpp` — スループット計測
  - ランダムトークン列生成
  - encode/decode MiB/s 測定
  - N=1 vs N=8 の A/B ベンチマーク
- [ ] 目標: >500 MiB/s (1コア, スカラー)

---

### Phase 3: AVX2 SIMD 実装
**目標**: rANS デコードを AVX2 で 8 状態同時処理

- [ ] `src/simd/x86_avx2/rans_decode_avx2.cpp`
- [ ] `src/simd/simd_dispatch.cpp` — ランタイム CPUID 検出
- [ ] SIMD vs スカラー A/B ベンチマーク
- [ ] `HAKONYANS_FORCE_SCALAR` 環境変数対応

---

### Phase 4: P-Index 並列デコード
**目標**: マルチスレッドでデコード速度がコア数に比例

- [ ] `src/entropy/nyans_p/pindex.h` — チェックポイント encode/decode
- [ ] `src/platform/thread_pool.cpp`
- [ ] 1/2/4/8 スレッドでのスケーリングベンチ
- [ ] `HAKONYANS_THREADS=N` 環境変数対応

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
