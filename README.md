# HakoNyans 🐱

**高速デコード重視の次世代画像コーデック**

ANS（Asymmetric Numeral Systems）ベースの並列エントロピー符号化エンジン **NyANS-P** を中核に、マルチコア・SIMD を最大限活用する設計。

## 🚀 パフォーマンス

| 指標 | 性能 | 備考 |
|------|------|------|
| **エントロピーデコード** | 516 MiB/s (LUT, 1-thread) | N=8 interleaved rANS |
| **並列スケーリング** | 5.17x @ 8 threads | P-Index による並列化 |
| **Full HD デコード** | 232 MiB/s | 1920×1080 RGB, end-to-end |
| **画質** | 49.0 dB @ Q100 | Grayscale, 8×8 block |

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
