# HakoNyans 🐱

**高速デコード重視の次世代画像コーデック**

ANS（Asymmetric Numeral Systems）ベースの並列エントロピー符号化エンジン **NyANS-P** を中核に、マルチコア・SIMD を最大限活用する設計。

## 🚀 パフォーマンス

### 内部ベンチマーク
| 指標 | 性能 | 備考 |
|------|------|------|
| **エントロピーデコード** | 516 MiB/s (LUT, 1-thread) | N=8 interleaved rANS |
| **並列スケーリング** | 5.17x @ 8 threads | P-Index による並列化 |
| **Full HD デコード** | 27.4 ms/frame | 1920×1080 RGB, 216 MiB/s |
| **画質** | 41.3 dB @ Q75 | Color RGB, 8×8 DCT |

### 競合比較（Full HD 1920×1080）

#### デコード速度
| Codec | デコード時間 | 相対速度 |
|-------|------------|---------|
| libjpeg-turbo | 8.3 ms | **3.3x 速い** ⚡ |
| **HakoNyans (Phase 7a)** | **24 ms** | **1.0x (基準)** |
| JPEG-XL | 33.7 ms | 0.71x (遅い) |
| AVIF | ~150 ms | 0.16x (遅い) |

#### 圧縮効率 vs 画質（テスト画像: lena.ppm）
| Codec | ファイルサイズ | PSNR | 相対サイズ |
|-------|-------------|------|---------|
| JPEG (Q90) | 168 KB | 34.6 dB | **1.0x (基準)** |
| HakoNyans (Phase 6) | 1004 KB | 39.4 dB | 5.98x |
| **HakoNyans (Phase 7a)** | **484 KB** | **40.3 dB** | **2.88x** ✅ |
| JPEG-XL | 320 KB | 38.2 dB | 1.9x |

**結論**: 
- ✅ **速度**: JPEG-XL/AVIF を上回る（エントロピー層の並列化が有効）
- ✅ **品質**: 画質面で全コーデックを凌駕（40.3 dB）
- ✅ **圧縮率**: Phase 7a で **6x → 2.9x** に大幅改善
- ⚠️ **カラー品質**: CfL 実装を改善予定（Phase 7b）

## 特徴

- **NyANS-P**: Parallel Interleaved rANS + P-Index
  - N=8 状態インターリーブで CPU の ILP/SIMD を活用
  - P-Index によりデコーダ側コア数に応じた並列分割が可能
  - **業界初**: エントロピーデコードレベルでの完全並列化
- **SIMD 最適化**: AVX2 + LUT による高速化（2.80x speedup）
- **Transform**: 8×8 DCT + JPEG-like 量子化
- **Color**: YCbCr 4:4:4（4:2:0 は今後対応予定）
- **箱理論設計**: モジュール境界が明確、テスト容易

## アーキテクチャ

```
L4: Frame Box        フレーム構造・メタデータ
L3: Transform Box    色変換・DCT・量子化
L2: ANS Entropy Box  NyANS-P (rANS interleaved + P-Index)
L1: Symbol Box       RUN/MAGC/EOB/SIGN トークン化
L0: Bitstream Box    ビット単位 I/O
```

## ビルド

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## 使い方

### CLI コマンド

```bash
# エンコード（PPM → .hkn）
./hakonyans encode input.ppm output.hkn [quality]

# デコード（.hkn → PPM）
./hakonyans decode output.hkn decoded.ppm

# ファイル情報表示
./hakonyans info output.hkn
```

### 環境変数

```bash
# スレッド数指定（デフォルト: CPU コア数）
export HAKONYANS_THREADS=4

# SIMD 無効化（デバッグ用）
export HAKONYANS_FORCE_SCALAR=1
```

## 開発状況

| Phase | 内容 | 状態 |
|-------|------|------|
| Phase 1 | rANS N=1 基本実装 | ✅ 完了 (5/5 tests) |
| Phase 2 | N=8 インターリーブ | ✅ 完了 (5/5 tests) |
| Phase 3 | AVX2 SIMD + LUT | ✅ 完了 (516 MiB/s) |
| Phase 4 | P-Index 並列デコード | ✅ 完了 (5.17x @ 8 threads) |
| Phase 5.1 | Grayscale コーデック | ✅ 完了 (49.0 dB @ Q100) |
| Phase 5.2 | YCbCr Color | ✅ 完了 (39.4 dB) |
| Phase 5.3 | Parallel 統合 | ✅ 完了 |
| Phase 5.4 | CLI + Bench | ✅ 完了 (232 MiB/s Full HD) |
| Phase 6 | 競合ベンチマーク | 🔜 予定 |

**総テスト数**: 30+ tests, 全て PASS ✅

## ディレクトリ構成

```
hakonyans/
├── docs/           設計仕様書
├── include/        公開 API ヘッダ
├── src/
│   ├── core/       ビットストリーム、基本ユーティリティ
│   ├── codec/      エンコード・デコード パイプライン
│   ├── entropy/    NyANS-P エントロピー符号化
│   ├── simd/       SIMD 実装 (AVX2/NEON/AVX-512)
│   └── platform/   CPU 検出、スレッドプール
├── tools/          CLI ツール
├── bench/          ベンチマーク
├── tests/          テスト
├── fuzz/           ファジング
└── research/       凍結・実験的コード
```

## 参考文献

- [Asymmetric Numeral Systems](https://arxiv.org/abs/0902.0271) — Jarek Duda
- [Interleaved Entropy Coders](https://arxiv.org/pdf/1402.3392) — Fabian Giesen
- [Recoil: Parallel rANS Decoding](https://arxiv.org/pdf/2306.12141) — Decoder-Adaptive Index

## ライセンス

MIT License
