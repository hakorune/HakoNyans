# NyANS-P: Parallel Interleaved rANS エントロピー符号化

## 概要

NyANS-P は HakoNyans の中核エントロピー符号化エンジン。
rANS を N 状態インターリーブし、P-Index で並列復号を可能にする。

## 理論的背景

- **rANS**: 1 つの整数状態でレンジ符号に匹敵する圧縮率と高速性を両立
  - [Asymmetric Numeral Systems (Duda, 2009)](https://arxiv.org/abs/0902.0271)
- **Interleaved**: 複数の rANS 状態を同一ストリームにインターリーブし ILP/SIMD を活用
  - [Interleaved Entropy Coders (Giesen, 2014)](https://arxiv.org/pdf/1402.3392)
- **P-Index**: 中間状態チェックポイントで任意位置から復号開始
  - [Recoil (2023)](https://arxiv.org/pdf/2306.12141)

## トークン化

量子化済み係数を小さなアルファベットに分解する：

| トークン | 意味 | rANS で符号化？ |
|---------|------|----------------|
| `EOB` | ブロック残り全部ゼロ | ✅ |
| `RUN(r)` | ゼロが r 個連続 (0..15) | ✅ |
| `RUN_ESC` | r > 15 の場合のエスケープ | ✅ (+ raw bits) |
| `MAGC(c)` | \|v\| のクラス (floor(log2(\|v\|))) | ✅ |
| `SIGN` | 符号ビット | ✅ |
| `REM` | クラス内の下位ビット | ❌ raw bits |

**設計意図**: rANS が扱うのは数十種類の小さな記号集合のみ。
`REM` を raw bits にすることで LUT サイズを抑え、L1 キャッシュに乗せる。

## インターリーブ (N=8)

```
トークン列:  t0  t1  t2  t3  t4  t5  t6  t7  t8  t9 ...
状態割当:    S0  S1  S2  S3  S4  S5  S6  S7  S0  S1 ...
```

- N=8 の rANS 状態 `S[0..7]` をラウンドロビンで割り当て
- 各状態は独立 → CPU のスーパースカラ/SIMD パイプラインで並列実行
- AVX2 の 256-bit レジスタで 8×32-bit 状態を同時処理可能

## rANS デコード（単一状態）

```
function rans_decode(state, cdf[], total):
    slot = state % total
    symbol = cdf_lookup(slot)        // LUT or alias table
    freq = cdf[symbol+1] - cdf[symbol]
    bias = cdf[symbol]
    state = freq * (state / total) + (state % total) - bias
    // renormalize
    while state < RANS_LOWER_BOUND:
        state = (state << 8) | read_byte()
    return symbol
```

## コンテキストモデル

並列性を殺さないため、コンテキストはチャンク内で閉じる：

```
ctx = (band_group, energy_class, qstep_class)
```

- `band_group`: 周波数帯域グループ (DC/LF/MF/HF)
- `energy_class`: ブロックのエネルギー分類 (低周波から決定)
- `qstep_class`: 量子化ステップの分類

## 開発ロードマップ

1. **Phase 1**: N=1 rANS 単体 encode/decode 往復テスト
2. **Phase 2**: N=8 interleave + bench_entropy
3. **Phase 3**: P-Index 追加 + マルチスレッド復号
4. **Phase 4**: コンテキスト適応モデル
