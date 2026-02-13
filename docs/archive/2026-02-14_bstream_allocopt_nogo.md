# 2026-02-14 no-go: byte_stream_encoder allocation/copy reduction

## Box
- Target: `src/codec/byte_stream_encoder.h`
- Intent:
  - replace per-call frequency-table allocation with reusable buffer
  - reduce output packing overhead (`insert` -> pre-sized contiguous writes)
  - keep format/bitstream unchanged

## Result
- Status: **no-go** (reverted)
- Reason: no stable single-core speed gain; reruns showed wall regression.

## Measurements
- Baseline:
  - `bench_results/phase9w_singlecore_blockclass_step2_scalar_final_vs_afterfix_20260214_runs3.csv`
- Trial run1:
  - `bench_results/phase9w_singlecore_bstream_allocopt_vs_step2_20260214_runs3.csv`
- Trial run2:
  - `bench_results/phase9w_singlecore_bstream_allocopt_vs_step2_20260214_runs3_rerun.csv`

Median comparison:
- `Enc(ms)`: `176.646 -> 177.171` (run1), `176.646 -> 181.886` (run2)
- `total HKN bytes`: unchanged (`2,977,544`)
- `median PNG/HKN`: unchanged (`0.261035`)

## Decision
- Revert patch and keep baseline implementation.

