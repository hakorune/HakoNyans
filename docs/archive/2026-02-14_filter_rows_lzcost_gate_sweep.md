# LZCOST Safety Gate Sweep Report (2026-02-14)

## Context
The `lzcost` filter row selection model was introduced to improve compression for LZ-friendly images (like `kodim03`), but it caused regressions in some photo images (like `nature_01`) because it bypassed the more accurate `ENTROPY` model.

## Implementation: Safe Gating
We implemented a safety gate and fallback mechanism:
1.  **Row Length Gate**: LZCOST is only evaluated for rows with >= 64 active pixels.
2.  **Profile Gate**: LZCOST is restricted to PHOTO profile by default (can be overridden).
3.  **Adoption Gate**: LZCOST winner is only adopted if `best_lz_cost * 1000 <= baseline_lz_cost * margin_permille`.
4.  **Baseline**: The adoption gate compares LZCOST against the `BITS2` (proxy) winner.
5.  **Fallback**: If LZCOST is not evaluated or rejected by the gate, the system falls back to the original model for that preset/profile (e.g., `ENTROPY` for MAX preset).

## Sweep Results (Re-verified after predictor unification)
Baseline (LZCOST OFF, MAX preset, runs=3): **2,956,913 bytes**

| Margin | TopK | Total Bytes | dTotal | Median PNG/HKN | Kodim03 Bytes | dKodim03 | Result |
|--------|------|-------------|--------|----------------|----------------|----------|--------|
| 980    | 3    | 2,956,155   | -758 B | 0.261194       | 368,989        | -758 B   | Win    |
| 990    | 3    | 2,955,616   | -1,297 B | 0.261433     | 368,450        | -1,297 B | Win    |
| 995    | 3    | 2,955,258   | -1,655 B | 0.261592     | 368,092        | -1,655 B | **Best** |
| 1000   | 3    | 2,956,975   | +62 B  | 0.260832       | 369,809        | +62 B    | Loss   |

## Conclusion
The combination of `TopK=3` and `Margin=995` provides the best compression in the current codebase while preserving roundtrip safety.

**Winning Parameters (current):**
- `HKN_FILTER_ROWS_LZCOST_TOPK=3`
- `HKN_FILTER_ROWS_LZCOST_MARGIN_PERMILLE=995`

`margin=980` is now roundtrip-safe and still improves size, but `995` gives better aggregate bytes.
