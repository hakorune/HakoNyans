# Phase 8c-v2 デバッグ結果

## 問題の根本原因

- `Screen Profile` 自体は発動していた（Copy/Palette ブロック多数）。
- サイズ肥大化の主因は以下の 2 点だった。
  - `copy_data` を `dx,dy` の生 4byte/block で保存しており、UI 系で 30K+ copy blocks に対して 100KB 超を消費。
  - `palette_data` が 2色ブロックで毎回 64bit インデックスを生保存し、同一/類似パターンの再利用ができていなかった。
- `rANS` 失敗や値域異常ではなかった（フィルタストリームサイズ・YCoCg 範囲ともに妥当）。

## 実施した修正

### 1) Copy stream 圧縮強化（`src/codec/copy.h`）
- 小ベクトル用に `mode=2` を追加。
- 使用ベクトル集合に応じて 0/1/2bit 可変符号化（従来は 2bit 固定）。
- 既存フォーマット互換を維持:
  - 旧 raw (`size == num_blocks*4`)
  - 旧 `mode=1`（2bit 固定）
  - 新 `mode=2`（動的コードブック）

### 2) Palette stream 圧縮強化（`src/codec/palette.h`）
- v2 ストリーム（magic `0x40`）を追加。
- `size==1`（単色）ブロックはインデックス payload を省略。
- `size==2` ブロックは 64bit マスク辞書化（有効時 1byte 参照）。
- 辞書化が不利な場合は自動で raw 64bit にフォールバック。
- 旧 v1 パレットストリームはデコーダで継続サポート。

## 修正後のベンチマーク結果

| Category | PNG (KB) | HKN (KB) | Ratio | Phase 8b比 |
|----------|----------|----------|-------|-----------|
| UI       | 10.0     | 33.8     | 3.20x | -91.8% |
| Anime    | 9.5      | 38.9     | 4.02x | -90.3% |
| Game     | 9.1      | 35.4     | 3.90x | -90.9% |
| Photo    | 1332.0   | 963.9    | 0.72x | -22.6% |

- 参考（UI 個別）:
  - `browser`: 2.15x
  - `vscode`: 4.52x
  - `terminal`: 2.93x

## ブロック分布（UI/browser）

`1920x1080 -> 240 x 135 = 32,400 blocks/plane`

- Y plane:
  - Copy: 31,924 blocks (98.53%)
  - Palette: 0 blocks (0.00%)
  - Filter: 476 blocks (1.47%)
- Co plane:
  - Copy: 32,309 blocks (99.72%)
  - Palette: 91 blocks (0.28%)
  - Filter: 0 blocks (0.00%)
- Cg plane:
  - Copy: 32,309 blocks (99.72%)
  - Palette: 91 blocks (0.28%)
  - Filter: 0 blocks (0.00%)
