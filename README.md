# HakoNyans 🐱

**高速デコード重視の次世代画像コーデック**

ANS（Asymmetric Numeral Systems）ベースの並列エントロピー符号化エンジン **NyANS-P** を中核に、マルチコア・SIMD を最大限活用する設計。

## 特徴

- **NyANS-P**: Parallel Interleaved rANS + Decoder-Adaptive Index
  - N=8 状態インターリーブで CPU の ILP/SIMD を活用
  - P-Index によりデコーダ側コア数に応じた並列分割が可能
- **SIMD ファースト**: AVX2 + NEON を本線、AVX-512 はボーナス
- **箱理論設計**: モジュール境界が明確、A/B テスト・段階的開発が容易

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

```bash
hakonyans encode input.png output.hkn
hakonyans decode output.hkn decoded.png
hakonyans info output.hkn
```

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
