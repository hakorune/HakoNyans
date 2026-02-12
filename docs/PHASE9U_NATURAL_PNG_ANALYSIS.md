# Phase 9u-next: Natural画像におけるPNG強圧縮要因分析

## 1. 背景と目的
Naturalカテゴリ（特にKodak画像やhd_01）において、PNGがHKN（Lossless）と比較して極端にファイルサイズが小さい（最大100倍以上の差）現象が確認された。
本分析では、その根本原因を特定し、HKNの次期改善に向けた指針（P0/P1/P2）を策定することを目的とする。

## 2. 分析対象
- `test_images/kodak/kodim01.ppm` (768x512)
- `test_images/kodak/kodim02.ppm` (768x512)
- `test_images/kodak/hd_01.ppm` (1920x1080)

## 3. 分析結果

### 3.1. 圧縮率の比較
| Image | PNG (KB) | HKN (KB) | Ratio (HKN/PNG) |
|---|---|---|---|
| kodim01 | 5.1 | 96.6 | 19.0x |
| kodim02 | 2.2 | 12.5 | 5.8x |
| hd_01 | 8.7 | 951.8 | 111.0x |

HKNはPNGに対して完敗しており、特に `hd_01` では100倍以上の差がついている。

### 3.2. PNGフィルタとエントロピー分析
Pythonスクリプトによるシミュレーション結果（zlib level 9）:

| Image | Raw(MB) | PNG(KB) | Res. Entropy | Limit(KB) | Ratio vs Limit | Top Filter |
|---|---|---|---|---|---|---|
| kodim01 | 1.18 | 5.1 | 0.354 bit | 52.2 | 0.10x | Paeth (50%), Up (50%) |
| kodim02 | 1.18 | 2.2 | 0.016 bit | 2.4 | 0.92x | Up (100%) |
| hd_01 | 6.22 | 8.7 | 0.921 bit | 716.4 | 0.01x | Paeth (72%), Up (27%) |

* **Res. Entropy**: フィルタ適用後の残差データの理論的エントロピー（bit/pixel）。
* **Limit(KB)**: エントロピー符号化のみで達成可能な理論限界サイズ。
* **Ratio vs Limit**: 実際のPNGサイズ / 理論限界サイズ。

**考察**:
1.  **フィルタの貢献**: `kodim01` や `hd_01` では、Paethフィルタ等が残差のエントロピーを劇的に下げている（8bit -> 0.35bit/0.92bit）。
2.  **LZ77の貢献**: `kodim01` (0.10x) や `hd_01` (0.01x) では、実際のサイズがエントロピー限界を遥かに下回っている。これは **LZ77による辞書圧縮（パターンの除去）** が支配的であることを示す。
    - `hd_01` の場合、エントロピー符号化だけでは716KBにしかならないが、LZ77によって8KBまで圧縮されている。これは画像内に同一パターン（おそらく人工的なテストパターンやフラット領域）が大量に含まれていることを示唆する。
3.  **例外**: `kodim02` はエントロピー限界（2.4KB）と実サイズ（2.2KB）が近く、LZの効果よりも「残差がほぼ0」であること（エントロピー自体の低さ）が支配的。

### 3.3. HKNの敗因分析
`bench_bit_accounting` の結果:
- **kodim01**: `filter_lo` が 81KB を占める。LZモード (Mode 2) が選択されているが、サイズ削減が不十分。
- **HKNの弱点**:
    1.  **TileLZの性能**: HKNの `TileLZ` は簡易的なLZ77実装であり、ハッシュ連鎖やLazy Matchingを持たない。また、出力形式が「タグ+生データ」であり、**LZ出力をハフマン/rANSで符号化していない**。
    2.  **LZとエントロピーの分断**: PNG (Deflate) は `LZ77 + Huffman` であり、LZのマッチ長や距離、リテラル自体もエントロピー符号化される。HKNの Mode 2 は `LZのみ`、Mode 0 は `rANSのみ` であり、両者の相乗効果を得られていない。
    3.  **フィルタ選択**: HKNもPaethを持っているが、SAD（絶対誤差和）ベースで選択しているため、LZ圧縮後のサイズを考慮できていない可能性がある。

### 3.4. 速度差（HKN vs PNG）観測結果
`bench_png_compare` の速度正面比較（固定6枚、runs=3/warmup=1）より:

- median `Enc(ms)` HKN/PNG: `337.691 / 107.825`（HKNは約`3.13x`遅い）
- median `Dec(ms)` HKN/PNG: `35.298 / 6.442`（HKNは約`5.48x`遅い）

HKN段階内訳（中央値）:
- Encode:
  - `plane_route_comp`: `138.299 ms`
  - `plane_block_class`: `92.062 ms`
  - `plane_lo_stream`: `65.373 ms`
- Decode:
  - `plane_filter_lo`: `16.451 ms`
  - `plane_reconstruct`: `5.970 ms`
  - `plane_filter_hi`: `2.394 ms`

**考察**:
1. PNG（libpng/deflate）は行フィルタ + DEFLATE で経路が比較的単純、かつ成熟実装に強く最適化されている。
2. HKN lossless は route競合・block分類・複数wrapper など探索/分岐が多く、encode側の固定費が大きい。
3. decode側でも `filter_lo` 復号と再構築ループが支配的で、現状は lossless 主経路の並列化が不足している。

## 4. 改善提案

Natural画像（特に人工的なパターンを含むもの）での競争力を高めるには、LZ77とエントロピー符号化の統合が不可欠である。

### P0: 互換維持（内部ロジック改善） - 期待改善率: 小
- **TileLZの探索強化**: ハッシュ連鎖やLazy Matchingを導入し、マッチ率を向上させる。
- **フィルタ選択の改善**: エントロピーベースまたはLZシミュレーションベースの選択基準を導入。
- **実装コスト**: 中。フォーマット変更なし。

### P1: 軽い拡張（LZ + rANS） - 期待改善率: 中〜大
- **filter_lo Mode 5 (LZ+rANS)** の追加:
    - `TileLZ` の出力をバイトストリームとして rANS で圧縮する。
    - これにより、LZのリテラルやマッチパラメータの偏りを圧縮できる。
    - `TileLZ` のフォーマット自体も、rANS向けに最適化（例えばリテラルとマッチ長を別ストリームにするなど）するとより効果的。
- **実装コスト**: 中。デコーダへのモード追加が必要。

### P2: 大きめ拡張（Deflate/Zstd統合） - 期待改善率: 特大
- **filter_lo Mode 6 (External LZ)** の追加:
    - `miniz` (Deflate) または `zstd` を直接利用して圧縮する。
    - PNGと同等以上の圧縮率が確実に保証される。
    - 特に `hd_01` のようなケースでは決定的な差となる。
- **実装コスト**: 大（ライブラリ依存の追加、ビルド構成の変更）。ただし `miniz` は既に一部で使用（Zipなど）されているなら導入は容易かもしれない。

## 5. 結論と次の方針
Naturalカテゴリでの大敗は、**「LZ77とエントロピー符号化の相乗効果欠如」** に尽きる。
HKNが汎用的な可逆圧縮としてPNGに対抗するには、**P1 (LZ + rANS)** または **P2 (Deflate採用)** の実装が必須である。
まずは **P1** 相当の「LZ出力をrANSで畳む」アプローチを `filter_lo` の新モードとして検討することを推奨する。

---
## 付録: 再現コマンド
```bash
# PNG分析ツールの実行
python3 tools/analyze_png_compression.py test_images/kodak/kodim01.ppm test_images/kodak/kodim02.ppm test_images/kodak/hd_01.ppm > bench_results/natural_png_analysis.csv

# HKNの内訳分析
./build/bench_bit_accounting test_images/kodak/kodim01.ppm --lossless > bench_results/kodim01_accounting.txt
```
