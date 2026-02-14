# Phase 9X-2: Mode 6 v0x0017 (Type Bitpack + Len Split) - Results

Date: 2026-02-14

## Implementation Summary

Successfully implemented Mode 6 v0x0017 format with type bitpacking and length stream splitting.

### Changes Made

1. **headers.h**: Version bump to 0x0017
   - Added VERSION_FILTER_LO_LZ_TOKEN_RANS_V3 = 0x0017
   - Updated VERSION constant

2. **lossless_filter_lo_codec.h**: v0x0017 encoding
   - Added parse_tilelz_to_tokens_v17() function
   - Type bit packing (1 bit/token: 0=LITRUN, 1=MATCH)
   - Split length streams: lit_len[] and match_len[]
   - 42-byte header with token/match/lit_token counts

3. **lossless_filter_lo_decode.h**: Triple version decode
   - v0x0017: 36 byte min payload, type_bits + lit_len + match_len
   - v0x0016: 28 byte min payload, compact dist (MATCH only)
   - v0x0015: 24 byte min payload, legacy format
   - Pre-check: token_count == lit_token_count + match_count

4. **lossless_mode_debug_stats.h**: Enhanced telemetry
   - filter_lo_mode6_v17_selected
   - filter_lo_mode6_typebits_raw_bytes_sum
   - filter_lo_mode6_typebits_enc_bytes_sum
   - filter_lo_mode6_lit_len_bytes_sum
   - filter_lo_mode6_match_len_bytes_sum

5. **bench/bench_bit_accounting.cpp**: v0x0017 diagnostics display

6. **bench/bench_png_compare.cpp**: CSV columns for Mode 6
   - hkn_enc_filter_lo_mode6_candidates
   - hkn_enc_filter_lo_mode6_wrapped_bytes
   - hkn_enc_filter_lo_mode6_reject_gate
   - hkn_enc_filter_lo_mode6_reject_best

7. **tests/test_lossless_round2.cpp**: Backward compat tests
   - test_filter_lo_mode6_v15_backward_compat()
   - test_filter_lo_mode6_v16_compact_dist() (NEW)
   - test_filter_lo_mode6_v17_typebit_lensplit() (NEW)

## Test Results

```
ctest: 17/17 PASS
```

## kodim03 Benchmark Results

### Mode 6 OFF (baseline)
- Total HKN: 369,747 bytes
- PNG/HKN ratio: 0.326

### Mode 6 ON
- Total HKN: 369,747 bytes (no change)
- Mode 6 candidates: 1
- Mode 6 wrapped bytes: 187,947
- Mode 6 reject gate: 1 (rejected by gate)
- Mode 6 reject best: 0

**Result**: Mode 6 was evaluated but rejected by the gain gate. kodim03 shows no improvement with current settings.

## Gate Verdict

### Required
1. ctest: 17/17 PASS - OK
2. max: total_hkn_bytes(on) <= off (369,747 = 369,747) - OK
3. max: median PNG/HKN(on) >= off (0.326 = 0.326) - OK
4. balanced: Mode 6 default OFF - OK
5. Mode6 off no-impact: pending

### Goal
- kodim03_bytes(on) < off - NOT ACHIEVED (369,747 = 369,747)

## Conclusion

**Status**: Needs continued work

The v0x0017 format is correctly implemented and passes all tests. However, kodim03 compression did not improve because Mode 6 was rejected by the gain gate (wrapped bytes exceeded the threshold).

This is acceptable per the plan: "kodim03 no improvement but others may improve"

Next steps:
1. Test on other image types (UI, anime, game screenshots)
2. Tune Mode 6 gate thresholds if needed
3. Continue optimization for natural images
