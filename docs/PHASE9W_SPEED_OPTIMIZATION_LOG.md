# Phase 9w Speed Optimization Log

This log records implementation details and benchmark results for the
Phase 9w speed work, in a format suitable for later paper write-up.

## 2026-02-12: Shared Thread Budget (Global Token Pool)

### Objective
- Keep compression unchanged.
- Reduce wall-time regressions caused by nested/overlapping `std::async` launches.
- Preserve coarse-grain parallelism while capping total spawned worker threads.

### Implementation
- Introduced global worker-token accounting in `src/platform/thread_budget.h`.
  - `ScopedThreadTokens::try_acquire_exact(n)`
  - `ScopedThreadTokens::try_acquire_up_to(max_n, min_n)`
  - tokens are returned automatically on scope exit (RAII).
- Replaced spawn gating from boolean checks to token lease checks:
  - `src/codec/encode.h`
  - `src/codec/decode.h`
  - `src/codec/lossless_filter_lo_codec.h`
  - `src/codec/lossless_filter_lo_decode.h`
  - `src/codec/lossless_block_classifier.h`
  - `src/codec/lossless_route_competition.h`
- Kept `ScopedParallelRegion` markers in async worker bodies for stage-level
  diagnostics and parallel-region tracing.

### Validation
- Build: `cmake --build . -j`
- Tests: `ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Protocol
- Command:
  - `./build/bench_png_compare --runs 3 --warmup 1 --baseline bench_results/phase9w_current.csv --out <csv>`
- Fixed 6 images.
- Main comparison uses rerun CSVs to reduce single-run jitter.

### Result Summary (rerun vs rerun)
- Previous stage:
  - `bench_results/phase9w_speed_stage_profile_after_thread_budget_rerun.csv`
- This stage:
  - `bench_results/phase9w_speed_stage_profile_after_thread_tokens_rerun.csv`

| Metric | Previous | This stage | Delta |
|---|---:|---:|---:|
| median Enc(ms) | 150.663 | 148.649 | -2.014 |
| median Dec(ms) | 18.416 | 17.958 | -0.458 |
| median PNG/HKN | 0.2610 | 0.2610 | 0.0000 |
| total HKN bytes | 2,977,544 | 2,977,544 | 0 |
| natural selected/candidates | 3/13 | 3/13 | no change |

### Notes
- Compression metrics stayed identical (expected; no format/rate logic changes).
- Decode improved consistently in this rerun.
- Encode recovered most of the regression seen after the previous
  thread-budget-only step.
- Remaining hotspots are still:
  - encode `plane_route_comp` and `plane_lo_stream`
  - decode `plane_filter_lo` and `plane_reconstruct`

### Repro Artifacts
- `bench_results/phase9w_speed_stage_profile_after_thread_tokens.csv`
- `bench_results/phase9w_speed_stage_profile_after_thread_tokens_rerun.csv`

## 2026-02-13: TileLZ Decode Fast-Path Trial (Not Promoted)

### Objective
- Validate ChatGPT-proposed "TileLZ decode fast-path first" idea with minimal surface area.
- Keep format and compression behavior unchanged.

### Trial Scope
- Prototype branch changes (later reverted) in:
  - `src/codec/lz_tile.h`
  - direct decode path rewrite with memcpy/memset-oriented match expansion
- Added permanent safety coverage in:
  - `tests/test_lossless_round2.cpp`
    - random-size TileLZ roundtrip matrix
    - overlap cases (`dist=1`, `dist=2`)
    - malformed stream failure check

### Validation
- Build and tests were green in trial:
  - `cmake --build . -j`
  - `ctest --output-on-failure`
  - `17/17 PASS`

### Result Summary
- Previous promoted stage:
  - `bench_results/phase9w_speed_stage_profile_after_thread_tokens_rerun.csv`
- Trial rerun:
  - `bench_results/phase9w_speed_stage_profile_after_tilelz_decode_fast_rerun.csv`

| Metric | Previous promoted | TileLZ trial | Delta |
|---|---:|---:|---:|
| median Enc(ms) | 148.649 | 149.935 | +1.286 |
| median Dec(ms) | 17.958 | 17.942 | -0.016 |
| median PNG/HKN | 0.2610 | 0.2610 | 0.0000 |
| total HKN bytes | 2,977,544 | 2,977,544 | 0 |

### Decision
- **Not promoted** (kept out of mainline): decode gain was negligible while encode median regressed.
- Kept only the additional TileLZ test coverage for future low-level iterations.

## 2026-02-13: Route-Comp Dedupe (Reuse Padded + Screen Map Optimization)

### Objective
- Reduce duplicate work in `plane_route_comp` candidate paths without changing format/rate.
- Specifically remove repeated pad/clamp work in screen/natural candidate encoders.

### Implementation
- Added padded-input entry points and routed candidate encode through them:
  - `src/codec/lossless_screen_route.h`
    - `encode_plane_lossless_screen_indexed_tile_padded(...)`
  - `src/codec/lossless_natural_route.h`
    - `encode_plane_lossless_natural_row_tile_padded(...)`
  - `src/codec/encode.h`
    - new wrappers in `GrayscaleEncoder`
    - route compete lambdas now capture prebuilt `padded`, `pad_w`, `pad_h`
- Reworked screen candidate histogram/index mapping:
  - replaced `unordered_map`-based counting/indexing with thread-local fixed-table
    mapping (`int16 -> index`) and explicit frequency sort (same ordering semantics).

### Validation
- Build: `cmake --build . -j`
- Tests: `ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- `bench_results/phase9w_speed_stage_profile_after_route_dedupe.csv`
- `bench_results/phase9w_speed_stage_profile_after_route_dedupe_rerun.csv`
- `bench_results/phase9w_speed_stage_profile_after_route_dedupe_rerun2.csv`

### Result Summary
- Compression metrics unchanged across runs:
  - `median PNG/HKN = 0.2610`
  - `total HKN bytes = 2,977,544`
- Speed effect is positive but within normal variance envelope:
  - previous promoted rerun:
    - `thread_tokens_rerun`: Enc `148.649`, Dec `17.958`
  - route-dedupe rerun #1:
    - Enc `148.597`, Dec `18.145`
  - route-dedupe rerun #2:
    - Enc `146.795`, Dec `17.672`

Interpretation:
- Encode side showed consistent or better behavior in repeated runs.
- Decode side is near-neutral with small run-to-run fluctuation.
