# 2026-02-14: mode2 optparse 2-pass cost model (No-Go)

## Scope
- Target: `src/codec/lossless_natural_route.h`
- Trial: replace flat Q8 byte-cost in `optparse_dp` with lazy1-output histogram derived cost table.
- Lane: `--preset max`, `HKN_MAX_LZ_MATCH_STRATEGY=2`.

## Baseline
- `bench_results/phase9w_max_lane_match_strategy1_after_dp_gate_20260214_runs3.csv`

## Candidate
- `bench_results/phase9w_max_lane_match_strategy2_dp_gate_costmodel_20260214_runs3.csv`

## Result
- size gain vs baseline: `-1,683 B`
- median Enc(ms): `359.208`
- Compared with flat-cost gated DP (`phase9w_max_lane_match_strategy2_dp_gate_final_20260214_runs3.csv`),
  gain was smaller and encode time did not improve.

## Decision
- No-Go.
- Reverted cost-model replacement and kept flat-cost DP implementation.
- Continue with threshold-gate sweeps instead.
