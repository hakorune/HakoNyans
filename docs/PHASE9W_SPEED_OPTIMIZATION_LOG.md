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

