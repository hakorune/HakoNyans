# 2026-02-13 plane_reconstruct COPY/TM4 fast path

## Objective

- `plane_reconstruct` の decode コストを低リスクで削減する。
- フォーマット互換と圧縮率非劣化を維持する。

## Scope

- 対象: `src/codec/lossless_plane_decode_core.h`
- 非対象: ビットストリーム形式、レート制御、自然経路判定

## Changes

- `COPY` ブロックで in-bounds 時に `memcpy` fast path を使用。
- `TILE_MATCH4` で in-bounds 時に 4-pixel 単位の `memcpy` fast path を使用。
- 境界ケースは既存の clamp フォールバックを維持。
- 補助最適化として `pad_w_i/pad_h_i` と `residual_size` をループ外へ。

## Validation

- Build: `cmake --build . -j`
- Test: `ctest --output-on-failure`
- Result: `17/17 PASS`

## A/B Evaluation

- baseline: `bench_results/phase9w_speed_stage_profile_after_route_dedupe_rerun2.csv`
- candidate: `bench_results/phase9w_speed_stage_profile_after_reconstruct_copytm4_memcpy_rerun.csv`

| Metric | Baseline | Candidate | Delta |
|---|---:|---:|---:|
| median Enc(ms) | 146.795 | 146.601 | -0.194 |
| median Dec(ms) | 17.672 | 18.047 | +0.375 |
| median PNG/HKN | 0.2610 | 0.2610 | 0.0000 |
| total HKN bytes | 2,977,544 | 2,977,544 | 0 |

## Decision

- **Archive (not promoted)**  
  サイズ/圧縮率は非劣化だが、decode の安定改善が確認できないため本採用は見送り。

## Notes

- 非採用試行の row-dispatch 版:
  - `bench_results/phase9w_speed_stage_profile_after_reconstruct_row_dispatch.csv`
  - `bench_results/phase9w_speed_stage_profile_after_reconstruct_row_dispatch_rerun.csv`
