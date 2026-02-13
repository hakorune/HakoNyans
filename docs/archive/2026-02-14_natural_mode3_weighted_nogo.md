# 2026-02-14 No-Go Archive: Natural mode3 + Weighted Predictor

## Summary
`mode3 (2-context CDF)` + `weighted predictor (A/B)` experiment is archived as **No-Go for balanced lane default**.

- Roundtrip: PASS (`ctest 17/17`)
- Compression gate: FAIL (fixed6 total bytes regression)

## Reproduced Result
Comparison: `bench_results/phase9w_final.csv` vs `bench_results/phase9w_weighted_final_v3.csv`

- `kodim01`: `53131 -> 52547` (`-584`)
- `hd_01`: `723466 -> 731345` (`+7879`)
- fixed6 total: `2977544 -> 2984839` (`+7295`)
- median `PNG/HKN`: `0.2610 -> 0.2610` (unchanged)

Conclusion: localized win exists, but total regression violates current balanced gate.

## Root Issues Found
1. Cost/implementation mismatch in filter path:
   - Estimator includes weighted filters (`6/7`), but row filter executor still implements only `0..5`.
2. Telemetry mismatch:
   - `bench_png_compare` natural mode selection columns are still `mode0/1/2` only.
3. Accounting mismatch:
   - `bench_bit_accounting` natural wrapper parser does not handle mode3 payload layout.

## Policy
- Keep this commit as experiment record.
- Do not promote these defaults to balanced lane until gate is green.
