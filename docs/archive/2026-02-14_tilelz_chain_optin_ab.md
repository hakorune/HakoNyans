# 2026-02-14 TileLZ chain/lazy opt-in A/B

## 目的
`TileLZ` の hash-chain + lazy 探索を導入しつつ、`balanced` の回帰を避ける。

## 実装
- 変更: `src/codec/lz_tile.h`
  - hash-chain + lazy1 探索を追加（env で有効化）
  - 旧シンプル探索（高速）を保持
  - env:
    - `HKN_TILELZ_CHAIN_DEPTH` (0..128)
    - `HKN_TILELZ_WINDOW_SIZE` (1024..65535)
    - `HKN_TILELZ_NICE_LENGTH` (3..255)
    - `HKN_TILELZ_MATCH_STRATEGY` (0=greedy,1=lazy1)
- デフォルト:
  - `CHAIN_DEPTH=0`
  - `MATCH_STRATEGY=0`
  - 強化探索は opt-in

## 回帰テスト
- `cmake --build build -j8`
- `ctest --test-dir build --output-on-failure`
- 結果: 17/17 PASS

## fixed6 (single-core, balanced, runs=3)

| config | CSV | total HKN bytes | median PNG/HKN | median Enc(ms) | median Dec(ms) |
|---|---|---:|---:|---:|---:|
| default (depth=0, strategy=0) | `bench_results/phase9w_singlecore_tilelz_optin_defaultsafe_20260214_runs3_rerun.csv` | 2,977,418 | 0.2610 | 213.150 | 13.080 |
| opt-in (depth=1, lazy1) | `bench_results/phase9w_singlecore_tilelz_optin_depth1_lazy1_20260214_runs3.csv` | 2,977,193 | 0.2658 | 222.870 | 13.119 |

差分（opt-in - default）:
- bytes: -225B（わずかに改善）
- Enc: +9.720ms（悪化）
- Dec: +0.039ms（ほぼ同等）

## 判定
- `balanced` デフォルトには採用しない（速度効率が不足）。
- hash-chain + lazy は `max/検証` 向けの opt-in として維持。
