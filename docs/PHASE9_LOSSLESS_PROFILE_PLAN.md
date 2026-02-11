# Phase 9: Lossless Auto Profile Plan

**Date**: 2026-02-11  
**Scope**: Lossless encoder profile auto-selection (encoder-side first, bit-exact維持)

## Goal

- 画像タイプ別に `Copy / Palette / Filter` の選択精度を上げる
- UI系（browser/vscode/terminal）のワースト圧縮率を底上げする
- Photo系の既存優位（PNG比 < 1.0）を維持する
- P0では既存ビットストリーム互換を壊さない

## A. Pattern Definition

### A-1. Block Features (8x8, YCoCg-R plane view)

| Feature | Definition | Purpose | Cost/block |
|---|---|---|---|
| `U` | 8x8内 `(Y,Co,Cg)` ユニーク色数 | Palette適性 | 64 insert/compare |
| `T` | ラスタ順で `p[i] != p[i-1]` 回数 | index map粗さ | 63 compares |
| `gX`,`gY` | Y水平方向/垂直方向差分絶対値和 | エッジ量/方向 | 112 abs |
| `A` | `256*abs(gX-gY)/(gX+gY+1)` | UI文字方向性 | O(1) |
| `varY`,`varC` | 分散近似 (`Co`,`Cg` は合算) | 写真/ノイズ検知 | 64 adds + mul |
| `CR` | `256*varC/(varY+1)` | 色ノイズ検知 | O(1) |
| `H` | block hash (64-bit) | Copy探索キー | 64 hash updates |
| `CopyHit` | 過去windowで hash一致かつ完全一致 | Copy採用条件 | dict lookup + cmp |

### A-2. Image-Level Pattern Rules (sampling)

サンプルは全ブロックの 1/16（x,yとも4刻み）を初期値とする。

| Pattern | Rule (sample stats) | Strong modes |
|---|---|---|
| Editor UI | `P(U<=16) >= 0.55` and `CopyRate >= 0.06` and `Mean(A) >= 120` | Copy + Palette + Filter |
| Browser UI | `P(U<=16) in [0.25,0.55)` and `MeanEdge >= 35` | Mixed (Filter寄り) |
| Flat UI | `P(U<=8) >= 0.45` and `MeanEdge <= 25` | Palette主導 |
| Pixel-art | `P(U<=16) >= 0.55` and `CopyRate >= 0.10` and `Mean(T) >= 28` | Copy主導 |
| Anime | `P(U<=16) >= 0.35` and `MeanEdge >= 30` and `Mean(A) >= 90` | Palette + Filter |
| Natural Photo | `P(U<=16) < 0.15` and `CopyRate < 0.02` | Filter主導 |

## B. Decision Logic

### B-1. Two-stage design

1. 画像全体の統計から profile を選択  
2. 各blockで `Copy -> Palette -> Filter` 順に確定（迷ったら Filter）

### B-2. Thresholds (P0 initial)

- Copy採用:
  - 完全一致のみ（lossless厳守）
  - `dy in [-32, 0]`, `dx in [-128, -1]`（同一行で `dx<0`）
  - `abs(dx) + 4*abs(dy) <= 160`
- Palette採用:
  - `U <= Kmax`（P0初期: `Kmax=16`）
  - `T <= 52` or `U <= 8`
  - `varY + varC <= 2,000,000`
- Filter:
  - 上記以外すべて
  - predictor は既存の5種（None/Sub/Up/Average/Paeth）

### B-3. Pseudocode (P0)

```cpp
mode = Filter;
if (copy_hit_exact && in_copy_window(dx, dy) && abs(dx) + 4 * abs(dy) <= 160) {
  mode = Copy;
} else if (U <= Kmax && (T <= 52 || U <= 8) && (varY + varC <= 2000000)) {
  mode = Palette;
}
```

## C. Algorithm Profiles

### C-1. Copy settings

| Level | Search window | Dict structure | Compare |
|---|---|---|---|
| P0 | `dx [-128,-1]`, `dy [-32,0]` | `hash -> last_pos` | exact only |
| P1 | `dx [-512,-1]`, `dy [-128,0]` | `hash -> vector<pos>(<=8)` | exact only |
| P2 | タイル因果領域全体 | LRU/chain | exact only |

### C-2. Palette settings

| Level | Kmax | Index coding |
|---|---|---|
| P0 | 16 | 現行方式 |
| P1 | profile別 (8/16/32) | 左/上文脈 + RLE |
| P2 | tile palette dict | dict ID + delta palette |

### C-3. Filter settings

| Level | Predictor policy |
|---|---|
| P0 | profileごと固定優先（安全） |
| P1 | blockごと5種評価 |
| P2 | plane別 + context強化 |

### C-4. Block type entropy

- アルファベット: `{Copy, Palette, Filter}`
- 文脈: `ctx = left_type + 3 * above_type`（9文脈）
- 符号化: NyANS-P（小アルファベット）

## D. Roadmap (P0/P1/P2)

### P0 (互換維持・即効)

1. 画像プロファイル判定（サンプル統計）
2. 閾値ベース block mode 決定木導入
3. Copy辞書 `hash -> last_pos`
4. タイル単位フォールバック（下記E-3）

### P1 (圧縮率強化)

1. Copy辞書を複数候補化（最大8）
2. Palette index の文脈/RLE化
3. Filter predictor を block単位最適化

### P2 (拡張)

1. Tile palette dictionary
2. Delta palette
3. ROI/部分復号向けメタ最適化

## E. Validation Protocol

### E-1. Ablation matrix

| ID | Copy | Palette | Filter選択 | Profile |
|---|---:|---:|---:|---:|
| Base | off | off | fixed | off |
| C-only | on | off | fixed | off |
| P-only | off | on | fixed | off |
| CP | on | on | fixed | off |
| Full-P0 | on | on | fixed | on |
| Full-P1 | on | on | block-opt | on |

### E-2. Success criteria (initial)

- UI worst-case改善:
  - browser: `2.15x -> >= 2.6x`
  - terminal: `2.93x -> >= 3.0x`
  - vscode: `4.52x` を大きく悪化させない（`>= 4.3x`）
- Anime/Game: 現状維持以上（+5%目安）
- Photo: Base(Filter-only)より悪化しない

### E-3. Safety fallback (tile-level)

各タイルについて:

1. `optimized_tile` を生成
2. `baseline_tile`（Filter-only）を生成
3. `optimized_bytes > baseline_bytes * 1.005` なら baseline 採用

これにより判定ミス時のワースト悪化を制限する。

## Notes

- P0は encoder-only 変更で進める（decoder互換維持）
- 新streamやheaderを追加する施策は P2 で仕様拡張として分離する
