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

## 2026-02-13: `plane_reconstruct` COPY/TM4 In-bounds Fast Path

### Objective
- Reduce decode cost in `plane_reconstruct` without touching format/rate logic.
- Start from low-risk paths only: `COPY` and `TILE_MATCH4`.

### Implementation
- Updated `src/codec/lossless_plane_decode_core.h`:
  - added in-bounds checks for `COPY` rows and `TILE_MATCH4` 4-pixel segments.
  - in in-bounds cases, replaced per-pixel clamp/copy loops with direct `memcpy`.
  - kept existing per-pixel clamped fallback for boundary cases.
  - hoisted `pad_w_i/pad_h_i` and `residual_size` to avoid repeated casts/size calls.

### Validation
- Build: `cmake --build . -j`
- Tests: `ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- `bench_results/phase9w_speed_stage_profile_after_reconstruct_copytm4_memcpy.csv`
- `bench_results/phase9w_speed_stage_profile_after_reconstruct_copytm4_memcpy_rerun.csv`
- (non-promoted trial) `bench_results/phase9w_speed_stage_profile_after_reconstruct_row_dispatch.csv`
- (non-promoted trial) `bench_results/phase9w_speed_stage_profile_after_reconstruct_row_dispatch_rerun.csv`

### Result Summary
- Comparison target:
  - `bench_results/phase9w_speed_stage_profile_after_route_dedupe_rerun2.csv`
- Latest rerun:
  - `bench_results/phase9w_speed_stage_profile_after_reconstruct_copytm4_memcpy_rerun.csv`

| Metric | Route-dedupe rerun2 | COPY/TM4 fast-path rerun | Delta |
|---|---:|---:|---:|
| median Enc(ms) | 146.795 | 146.601 | -0.194 |
| median Dec(ms) | 17.672 | 18.047 | +0.375 |
| median PNG/HKN | 0.2610 | 0.2610 | 0.0000 |
| total HKN bytes | 2,977,544 | 2,977,544 | 0 |

### Decision
- Size/ratio invariants are preserved.
- Decode gain is not yet stable against run-to-run noise; this is **not promoted as a clear speed win**.
- Next work should move to larger hotspots (`plane_filter_lo`, route-comp encode cost), with stronger per-stage counters before further decode micro-tuning.

## 2026-02-13: Route Policy + Partial Plane Parallelism

### Objective
- Reduce wasted route-competition work ("low-effect boxes") without changing format.
- Improve robustness of plane-level parallelism under constrained worker tokens.

### Implementation
- `src/codec/encode.h`
  - Added route policy env parsing:
    - `HKN_ROUTE_COMPETE_CHROMA` (default: `true`)
    - `HKN_ROUTE_COMPETE_PHOTO_CHROMA` (default: `false`)
    - `HKN_ROUTE_COMPETE_CHROMA_CONSERVATIVE` (default: `false`, experimental)
  - Added conservative chroma route gate knobs (env-overridable):
    - `HKN_ROUTE_CHROMA_MAD_MAX` (default: `80`)
    - `HKN_ROUTE_CHROMA_AVG_RUN_MIN` (default: `320`)
  - Extended `encode_plane_lossless(...)` with route-policy controls:
    - `enable_route_competition`
    - `conservative_chroma_route_policy`
  - Added `route_compete_policy_skip_count` accounting when policy skips route-comp.
  - Plane-parallel execution now uses `try_acquire_up_to(3, 2)`:
    - 3 tokens: Y/Co/Cg all async
    - 2 tokens: Y+Co async, Cg on caller thread
    - else: sequential fallback
- `src/codec/lossless_mode_debug_stats.h`
  - Added `route_compete_policy_skip_count`.
- `bench/bench_bit_accounting.cpp`
  - Added `Route policy diagnostics` section to print skip counts.

### Validation
- Build: `cmake --build . -j` (in `build/`)
- Tests: `ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- `bench_results/phase9w_speed_stage_profile_after_route_policy_parallel_partial.csv`
- `bench_results/phase9w_speed_stage_profile_after_route_policy_parallel_partial_rerun.csv`
- `bench_results/phase9w_speed_stage_profile_after_route_policy_parallel_partial_rerun2.csv`

### Result Summary (vs route-dedupe rerun2)
- Reference:
  - `bench_results/phase9w_speed_stage_profile_after_route_dedupe_rerun2.csv`
- New runs:
  - `bench_results/phase9w_speed_stage_profile_after_route_policy_parallel_partial*.csv`

Observed:
- Compression invariants held in all runs:
  - `median PNG/HKN = 0.2610`
  - `total HKN bytes = 2,977,544`
- `plane_route_comp` median stage time dropped in two runs (`~90ms` vs `130.674ms` baseline), but wall-clock medians showed large host-side variance (`Enc 134.6ms` to `188.1ms`).

### Decision
- Keep implementation for further controlled measurement (policy hooks + parallel fallback logic are correct and tested).
- Do not claim a promoted speed win until reruns under tighter noise control.

## 2026-02-12: Deep Observation Counters (filter_lo / reconstruct / parallel scheduler)

### Objective
- Add deeper observability before further speed tuning.
- Make hotspot attribution explicit inside `plane_filter_lo` and `plane_reconstruct`.

### Implementation
- `src/codec/lossless_decode_debug_stats.h`
  - Added decode parallel scheduler counters:
    - `decode_plane_parallel_3way_count`, `decode_plane_parallel_seq_count`, `decode_plane_parallel_tokens_sum`
    - `decode_ycocg_parallel_count`, `decode_ycocg_sequential_count`, `decode_ycocg_parallel_threads_sum`
  - Added `filter_lo` decode internals:
    - mode counters (`raw/1/2/3/4/5/invalid`)
    - fallback/zero-pad counters
    - inner timing (`decode_rans/shared_rans/tilelz`)
    - mode4 parallel-context counters
  - Added `reconstruct` internals:
    - COPY/TM4 fast-vs-slow path counts
    - clamp-path pixel counts
    - residual consumed/missing counters
- `src/codec/lossless_filter_lo_decode.h`
  - Added timed wrappers for `decode_byte_stream`, `decode_byte_stream_shared_lz`, and TileLZ decompress.
  - Added mode-wise counter accounting and fallback tracking.
- `src/codec/lossless_plane_decode_core.h`
  - Wired decode stats into `decode_filter_lo_stream(...)`.
  - Added reconstruct fast/slow counters for COPY/TM4 and residual missing counts.
- `src/codec/encode.h`, `src/codec/decode.h`, `src/codec/lossless_mode_debug_stats.h`
  - Added encode/decode plane-parallel scheduler counters.
- `bench/bench_png_compare.cpp`
  - Added new CSV columns and console sections:
    - plane Y/Co/Cg timings
    - parallel scheduler counters
    - decode deep counters (filter_lo modes, inner timings, reconstruct fast/slow)
- `bench/bench_bit_accounting.cpp`
  - Added `Encode parallel diagnostics` output.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`
- Smoke bench:
  - `build/bench_png_compare --runs 1 --warmup 0 --out bench_results/tmp_stage_profile_deep_counters.csv`

### Note
- This change is instrumentation-focused.
- No format change, no intended compression/quality behavior change.

## 2026-02-12: Deep-Counter Readout and Attack Order

### Measurement
- Command:
  - `build/bench_png_compare --runs 3 --warmup 1 --out bench_results/tmp_stage_profile_deep_counters_runs3.csv`
- Key medians (fixed 6):
  - `median Enc(ms) = 132.797`
  - `median Dec(ms) = 19.020`
  - decode stage:
    - `plane_filter_lo = 15.564 ms`
    - `plane_reconstruct = 5.679 ms`
  - decode deep internals:
    - `filter_lo_decode_rans = 15.165 ms`
    - `filter_lo_decode_shared_rans = 0.000 ms`
    - `filter_lo_tilelz = 0.000 ms`
  - reconstruct path counters:
    - COPY slow-path: effectively `0` in median
    - TM4 slow-path: effectively `0` in median
    - `residual_missing = 0`

### Interpretation
- Current decode bottleneck is rANS byte-stream decode inside `plane_filter_lo`.
- `plane_reconstruct` has already good fast-path hit rates; it is a secondary target.
- Parallel scheduler counters show 3-way plane parallelism is active in both encode/decode; primary issue is per-core work cost, not missing parallel dispatch.

### Attack Order (fixed)
1. `decode_byte_stream` / `decode_byte_stream_shared_lz` fast path (LUT decode path first)
2. `plane_route_comp` pruning / early exit for low-effect candidates
3. Additional reconstruct micro-optimizations only after (1)(2)

## 2026-02-12: rANS LUT Decode Fast Path

### Objective
- Reduce decode hotspot in `plane_filter_lo` without changing bitstream format.
- Replace per-symbol linear symbol search with LUT lookup path already available in `FlatInterleavedDecoder`.

### Implementation
- `src/codec/decode.h`
  - `decode_byte_stream(...)`
    - Added basic payload bounds checks (`cdf_size`, `rans_size`).
    - Switched output build from `reserve + push_back` to `resize + direct store`.
    - Added LUT decode path:
      - build `SIMDDecodeTable` once per stream (`CDFBuilder::build_simd_table`)
      - decode via `decode_symbol_lut(...)` for `count >= 128`
  - `decode_byte_stream_shared_lz(...)`
    - Switched to `resize + direct store`.
    - Added static shared LUT table (`get_mode5_shared_lz_simd_table()`).
    - decode via `decode_symbol_lut(...)`.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_stage_profile_deep_counters_runs3.csv`
- candidate:
  - `bench_results/tmp_stage_profile_deep_counters_lut_runs3.csv`
  - `bench_results/tmp_stage_profile_deep_counters_lut_runs3_rerun.csv`

### Result Summary
- Compression invariants:
  - `median PNG/HKN = 0.2610` (unchanged)
  - total `HKN_bytes` unchanged (all 6 images)
- Decode improvement is large and stable in stage counters:
  - `plane_filter_lo` median: `15.564 ms` -> `6.059 ms` (rerun `6.151 ms`)
  - `filter_lo_decode_rans` median: `15.165 ms` -> `5.536 ms` (rerun `5.908 ms`)
  - `median Dec(ms)`: `19.020` -> `12.696` (rerun `11.930`)
- Encode wall-clock showed host variance across runs; no encode logic was changed in this patch.

### Decision
- Keep and promote this optimization as decode-hotspot reduction.
- Next step remains `plane_route_comp` pruning for encode wall-clock.

## 2026-02-12: Natural Route Mode1/Mode2 Compute Dedupe

### Objective
- Reduce route competition cost without affecting bitstream decisions.
- Remove duplicated pixel-loop work in natural route mode1/mode2 generation.

### Implementation
- `src/codec/lossless_natural_route.h`
  - Added `Mode1Prepared` and `build_mode1_prepared(...)`.
    - Computes predictor IDs + residual bytes once for mode1 predictor set.
  - Added `build_mode1_payload_from_prepared(...)`.
    - Builds mode1/mode2 wrappers from shared prepared data.
  - Updated `encode_plane_lossless_natural_row_tile_padded(...)`:
    - mode1/mode2 now reuse the same prepared predictor/residual streams.
  - Legacy wrapper `build_mode1_payload(...)` remains available and now uses the shared helpers.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_stage_profile_deep_counters_lut_runs3_rerun.csv`
- candidate:
  - `bench_results/tmp_stage_profile_natural_mode_dedupe_runs3_ab.csv`
  - `bench_results/tmp_stage_profile_natural_mode_dedupe_runs3_rerun.csv`

### Result Summary
- Compression invariants:
  - all 6 images `HKN_bytes` unchanged
  - `median PNG/HKN = 0.2610` unchanged
- Stage diagnostics:
  - `plane_route_comp` showed consistent reduction in A/B run (`median dRoute ~= -4.3 ms` across per-image diffs).
- Wall-clock:
  - run-to-run variance remains large; treat wall-clock gain as promising but not yet fully stabilized.

### Decision
- Keep this change (safe compute dedupe, no compression impact).
- Continue with additional route-comp pruning where measurement noise can be reduced by tighter benchmark control.

## 2026-02-13: Route-Comp Deep Counters + Natural Mode1/Mode2 Parallel

### Objective
- Make `plane_route_comp` bottlenecks directly observable (prefilter vs screen vs natural).
- Reduce natural candidate wall-time without changing container format or size policy.

### Implementation
- `src/codec/lossless_mode_debug_stats.h`
  - Added route-comp deep encode counters:
    - `perf_encode_plane_route_prefilter_ns`
    - `perf_encode_plane_route_screen_candidate_ns`
    - `perf_encode_plane_route_natural_candidate_ns`
    - `perf_encode_plane_route_parallel_count`
    - `perf_encode_plane_route_seq_count`
    - `perf_encode_plane_route_parallel_tokens_sum`
- `src/codec/lossless_route_competition.h`
  - Instrumented route-comp stages with nanosecond counters.
  - Added scheduler counters for parallel-vs-sequential route candidate execution.
- `src/codec/lossless_natural_route.h`
  - Added conditional parallel build for mode1/mode2 payload generation
    (`pixel_count >= 262144` and worker token available).
  - Optimized mode2 global-chain LZ hash-head initialization using thread-local epoch table
    (remove per-call full head reset cost).
- `src/codec/lossless_screen_route.h`
  - Replaced index `push_back` loop with direct write into pre-sized vector
    (small allocation/branch reduction).
- `bench/bench_png_compare.cpp`
  - Exported new route-comp deep counters to CSV and console stage breakdown.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- `bench_results/tmp_next_move_route_obs_opt.csv`
- `bench_results/tmp_next_move_route_obs_opt_rerun.csv`
- `bench_results/tmp_next_move_route_obs_opt_rerun2.csv`

### Result Summary
- Baseline:
  - `bench_results/tmp_current_lossless_status.csv`
- Latest rerun:
  - `bench_results/tmp_next_move_route_obs_opt_rerun2.csv`

| Metric | Baseline | Latest rerun | Delta |
|---|---:|---:|---:|
| median PNG/HKN | 0.2610 | 0.2610 | 0.0000 |
| total HKN bytes | 2,977,544 | 2,977,544 | 0 |
| median Enc(ms) | 138.985 | 136.398 | -2.587 |
| median `plane_route_comp`(ms) | 85.747 | 65.545 | -20.202 |
| median Dec(ms) | 11.593 | 13.519 | +1.926 |

Route-comp deep breakdown (latest rerun):
- `route_prefilter`: `0.190 ms`
- `route_screen`: `0.000 ms`
- `route_natural`: `56.346 ms`
- route scheduler median: `parallel/seq/tokens = 0/3/0`

Interpretation:
- The dominant encode hotspot inside route-comp is now explicitly confirmed as
  natural candidate construction/compression.
- Compression invariants are preserved.
- Decode median remained unstable vs the earlier baseline run; because most images
  did not adopt alternate routes and size stayed identical, this needs controlled
  reruns (CPU pinning / fixed governor) before attributing causality.

## 2026-02-13: Natural Route Compute Pipeline Dedupe + Prep Parallel

### Objective
- Further reduce `route_natural` wall-time while keeping bitstream decisions unchanged.
- Remove repeated predictor-stream compression work and overlap independent prep stages.

### Implementation
- `src/codec/lossless_natural_route.h`
  - Added `PackedPredictorStream` and `build_packed_predictor_stream(...)`.
    - Predictor stream (raw vs rANS) is now built once and reused by mode1/mode2 payload builders.
  - Updated `build_mode1_payload_from_prepared(...)` to take packed predictor data directly
    (removes duplicated predictor encoding on mode1/mode2 path).
  - Reworked global-chain LZ `prev` buffer to thread-local reusable storage
    (avoids per-call allocation/initialization overhead).
  - Added conditional parallel prep in `encode_plane_lossless_natural_row_tile_padded(...)`:
    - `mode0` payload build and `mode1_prepared` build run in parallel for large tiles
      (`pixel_count >= 262144`) when one worker token is available.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_next_move_route_obs_opt_rerun2.csv`
- candidate:
  - `bench_results/tmp_next_move_natural_parallelprep.csv`
  - `bench_results/tmp_next_move_natural_parallelprep_rerun.csv`

### Result Summary
- Compression invariants:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Latest rerun (`tmp_next_move_natural_parallelprep_rerun.csv`) vs baseline:
  - `median Enc(ms): 136.398 -> 124.480` (`-11.918`)
  - `median plane_route_comp(ms): 65.545 -> 62.257` (`-3.288`)
  - `median route_natural(ms): 56.346 -> 51.249` (`-5.097`)

Interpretation:
- `route_natural` time was reduced with output-size invariants preserved.
- Decode medians remained noisy across runs and should continue to be treated as
  host-side variance unless reproduced under fixed benchmark environment controls.

## 2026-02-13: Natural Route Deep Observation Counters

### Objective
- Add internal counters for natural route build stages to decide the next optimization target.
- Keep output format and size behavior unchanged.

### Implementation
- `src/codec/lossless_mode_debug_stats.h`
  - Added natural-route deep counters:
    - mode stage timings (`mode0`, `mode1_prepare`, `pred_pack`, `mode1`, `mode2`)
    - selected mode counters (`mode0/1/2`)
    - predictor stream mode counters (`raw/rANS`)
    - mode2 bias gate counters (`adopt/reject`)
    - prep and mode1/2 parallel scheduler counters.
- `src/codec/lossless_natural_route.h`
  - Wired timing/scheduler/selection counter updates.
  - Added optional stats pointer pass-through for natural route functions.
- `src/codec/encode.h`
  - Passed `LosslessModeDebugStats` pointer into route natural candidate build.
- `bench/bench_png_compare.cpp`
  - Exported natural deep counters to CSV and printed them in stage/parallel sections.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- `bench_results/tmp_measure_natural_deep_20260213.csv`
- `bench_results/tmp_measure_natural_deep_20260213_rerun.csv`

### Result Summary (latest rerun)
- `median PNG/HKN = 0.2610` (unchanged)
- `total HKN bytes = 2,977,544` (unchanged)
- `median Enc(ms) HKN/PNG = 114.150 / 108.586`
- `plane_route_comp = 63.345 ms`
  - `route_natural = 52.410 ms`
    - `nat_mode0 = 18.502 ms`
    - `nat_mode1_prepare = 9.910 ms`
    - `nat_pred_pack = 0.012 ms`
    - `nat_mode1 = 13.235 ms`
    - `nat_mode2 = 32.870 ms`
- natural mode selected (median): `mode0/mode1/mode2 = 0/0/1`

Interpretation:
- Route natural remains dominated by `mode2` generation cost.
- The next high-ROI target is mode2 internals (global-chain search and/or residual production path).

## 2026-02-13: Route-Natural Pipeline Overlap (Consult Plan #1)

### Objective
- Start `mode2` as soon as `mode1_prepared` is available, instead of waiting for
  the separate mode1/mode2 phase.
- Preserve format/bitstream choice logic and size behavior.

### Implementation
- `src/codec/lossless_natural_route.h`
  - Added pipeline path for large tiles (`pixel_count >= 262144`) when one worker token is available:
    - worker: `mode1_prepared -> pred_pack -> mode2`
    - main: `mode0 -> mode1`
    - then join and run existing best-selection logic unchanged.
  - Kept non-pipeline fallback path (existing prep/mode12 scheduling behavior).
  - Kept deep counters updated for prep/mode12 timings and scheduler counts.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_measure_natural_deep_20260213_rerun.csv`
- candidate:
  - `bench_results/tmp_pipeline_overlap_route_natural.csv`
  - `bench_results/tmp_pipeline_overlap_route_natural_rerun.csv`
  - `bench_results/tmp_pipeline_overlap_route_natural_rerun2.csv`

### Result Summary
- Compression invariants:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Candidate runs showed one noisy outlier in wall-clock, but stage counters were consistently lower:
  - run#1 (`tmp_pipeline_overlap_route_natural.csv`)
    - `median Enc(ms): 114.150 -> 105.304` (`-8.846`)
    - `median plane_route_comp(ms): 63.345 -> 53.973` (`-9.372`)
    - `median route_natural(ms): 52.410 -> 44.005` (`-8.405`)
  - run#2 (`tmp_pipeline_overlap_route_natural_rerun.csv`)
    - `median Enc(ms): 114.150 -> 119.288` (`+5.138`, noisy host run)
    - `median plane_route_comp(ms): 63.345 -> 53.600` (`-9.745`)
    - `median route_natural(ms): 52.410 -> 43.542` (`-8.868`)
  - run#3 (`tmp_pipeline_overlap_route_natural_rerun2.csv`)
    - `median Enc(ms): 114.150 -> 103.957` (`-10.193`)
    - `median plane_route_comp(ms): 63.345 -> 53.953` (`-9.392`)
    - `median route_natural(ms): 52.410 -> 42.300` (`-10.110`)

Interpretation:
- The pipeline overlap reduced route-natural wall time materially while keeping
  the existing mode-selection/size outcomes unchanged.
- This supports consult Plan #1 as a high-ROI, low-risk optimization; wall-clock
  should be judged from repeated runs because host variance remains non-trivial.

## 2026-02-13: Documentation Sync (Post-Plan1)

### Current Promoted Baseline For Next Work
- Use this CSV as baseline for subsequent A/B:
  - `bench_results/tmp_pipeline_overlap_route_natural_rerun2.csv`
- Current target hotspot:
  - `route_natural` internals, especially `nat_mode2`.

### Next Implementation Target (Consult Plan #2)
- Optimize `compress_global_chain_lz(...)` in `src/codec/lossless_natural_route.h`
  with bitstream-preserving micro-optimizations:
  - reduce avoidable realloc/copy overhead in literal flush path
  - speed up match-length extension loop without changing tie-break/selection logic
- Non-goals:
  - no format change
  - no mode selection policy change
  - no compression regression

### Acceptance Gate (unchanged)
- `ctest` 17/17 PASS
- fixed-6:
  - `median PNG/HKN` non-regression
  - `total HKN bytes` non-regression
  - `route_natural` / `plane_route_comp` stage-time improvement preferred over
    noisy single-run wall-clock conclusions.

## 2026-02-13: Mode2 Global-Chain LZ Micro-Optimization (Consult Plan #2)

### Objective
- Speed up `route_natural` mode2 payload generation without changing:
  - format
  - mode selection policy
  - compressed size behavior

### Implementation
- `src/codec/lossless_natural_route.h`
  - `compress_global_chain_lz(...)` optimized with bitstream-preserving micro changes:
    - added `<cstring>` and switched to pointer-local source access (`const uint8_t* s`)
    - increased `out.reserve(...)` headroom to reduce reallocations on match-heavy streams
    - replaced literal flush `push_back + insert` with `resize + memcpy`
    - replaced byte-by-byte match extension with:
      - 8-byte compare loop (`memcpy`-based load, bounds-safe)
      - trailing byte loop for exact tail length
  - Match accept/tie-break logic remains unchanged:
    - same chain traversal order
    - same `(len > best_len) || (len == best_len && dist < best_dist)` rule
    - same `len==3` distance gate (`min_dist_len3`)

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_pipeline_overlap_route_natural_rerun2.csv`
- candidates:
  - `bench_results/tmp_mode2_lz_opt_20260213.csv` (noisy wall-clock run)
  - `bench_results/tmp_mode2_lz_opt_20260213_rerun.csv` (rerun used for promoted comparison)

### Result Summary (rerun promoted)
- Compression invariants:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Median deltas vs baseline (`tmp_pipeline_overlap_route_natural_rerun2.csv`):
  - `hkn_enc_ms`: `103.956844 -> 98.445622` (`-5.511222`)
  - `hkn_dec_ms`: `14.057877 -> 13.268487` (`-0.789390`)
  - `hkn_enc_plane_route_ms`: `53.953024 -> 48.661896` (`-5.291128`)
  - `hkn_enc_plane_route_natural_candidate_ms`: `42.299897 -> 38.462874` (`-3.837023`)
  - `hkn_enc_route_nat_mode2_ms`: `32.525301 -> 27.274424` (`-5.250877`)

Interpretation:
- The mode2 hotspot was materially reduced while keeping fixed-6 size metrics identical.
- This confirms Plan #2 as a safe throughput win following Plan #1 pipeline overlap.

### Current Promoted Baseline For Next Work
- Use this CSV as baseline for subsequent A/B:
  - `bench_results/tmp_mode2_lz_opt_20260213_rerun.csv`

## 2026-02-13: Mode0/Mode1Prep Scan Simplification (Consult Plan #3)

### Objective
- Reduce `route_natural` pre-compute overhead (mode0 / mode1prep) while preserving:
  - format
  - selection policy
  - compressed size behavior

### Implementation
- `src/codec/lossless_natural_route.h`
  - `build_mode0_payload(...)`
    - removed temporary `recon` allocation and switched to direct row pointers (`row`, `up_row`)
    - merged predictor cost evaluation into a single pass (`cost0/cost1/cost2`)
    - switched residual output from `push_back` to `resize + pointer write`
    - moved per-pixel predictor branch to per-row branch (`best_p` specialized loops)
  - `build_mode1_prepared(...)`
    - removed temporary `recon` allocation
    - merged predictor cost evaluation into a single pass (`cost0..cost4`)
    - switched residual output to contiguous pointer writes
    - moved predictor switch to per-row specialized loops for residual generation

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_mode2_lz_opt_20260213_rerun.csv`
- candidates:
  - `bench_results/tmp_plan3_mode01prep_opt_20260213.csv`
  - `bench_results/tmp_plan3_mode01prep_opt_20260213_rerun.csv`
  - `bench_results/tmp_plan3_mode01prep_opt_20260213_runs5.csv`
  - `bench_results/tmp_plan3_mode01prep_opt_20260213_v2.csv` (promoted)

### Result Summary (promoted run: v2)
- Compression invariants:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Median deltas vs baseline (`tmp_mode2_lz_opt_20260213_rerun.csv`):
  - `hkn_enc_ms`: `98.445622 -> 97.609977` (`-0.835645`)
  - `hkn_dec_ms`: `13.268487 -> 13.566274` (`+0.297787`)
  - `hkn_enc_plane_route_ms`: `48.661896 -> 44.292525` (`-4.369371`)
  - `hkn_enc_plane_route_natural_candidate_ms`: `38.462874 -> 34.149253` (`-4.313621`)
  - `hkn_enc_route_nat_mode0_ms`: `17.431942 -> 12.898642` (`-4.533300`)
  - `hkn_enc_route_nat_mode1prep_ms`: `10.400007 -> 4.956338` (`-5.443669`)

Interpretation:
- Stage-level natural-route precompute time was materially reduced.
- Wall-clock remains host-noise sensitive, so stage counters remain the primary gate.

### Current Promoted Baseline For Next Work
- Use this CSV as baseline for subsequent A/B:
  - `bench_results/tmp_plan3_mode01prep_opt_20260213_v2.csv`

## 2026-02-13: Decode Observation Deepening (YCoCg + Scheduler Split)

### Objective
- Strengthen decode-side observation before further optimization.
- Split wall-clock-heavy decode stages into:
  - scheduler/dispatch overhead
  - kernel compute
  - wait/join blocking

### Implementation
- `src/codec/lossless_decode_debug_stats.h`
  - Added decode counters:
    - `decode_plane_dispatch_ns`
    - `decode_plane_wait_ns`
    - `decode_ycocg_dispatch_ns`
    - `decode_ycocg_kernel_ns`
    - `decode_ycocg_wait_ns`
    - `decode_ycocg_rows_sum`
    - `decode_ycocg_pixels_sum`
- `src/codec/decode.h`
  - `decode_color_lossless(...)`:
    - instrumented plane 3-way scheduler dispatch/wait windows
    - instrumented YCoCg->RGB dispatch/kernel/wait split
    - accumulated per-task rows/pixels for parallel path
- `bench/bench_png_compare.cpp`
  - Exported new counters to CSV.
  - Added median print lines in stage/parallel sections for the new splits.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Observation Artifact
- `bench_results/tmp_obs_ycocg_deep_20260213.csv`
  - command:
    - `./build/bench_png_compare --runs 1 --warmup 0 --baseline bench_results/tmp_plan3_mode01prep_opt_20260213_v2.csv --out bench_results/tmp_obs_ycocg_deep_20260213.csv`

### Initial Readout (runs=1, diagnostic)
- New median lines observed in console:
  - `plane dispatch/wait`
  - `ycocg dispatch/kernel/wait`
  - `ycocg rows/pixels`
- Example (diagnostic run):
  - `plane dispatch/wait: 0.000 / 7.829 ms`
  - `ycocg dispatch/kernel/wait: 5.228 / 2.043 / 0.728 ms`
  - `ycocg rows/pixels: 796 / 1,233,408`

Interpretation:
- The decode bottleneck is not only arithmetic kernel time; scheduler and wait portions are now explicitly measurable.
- Next optimization should target the largest stable component after repeated-runs observation (especially ycocg dispatch/wait behavior).

## 2026-02-13: YCoCg->RGB Dispatch Overhead Reduction

### Objective
- Reduce decode wall time by lowering YCoCg->RGB scheduling overhead.
- Keep format and size behavior unchanged (decode-side only optimization).

### Implementation
- `src/codec/decode.h` (`decode_color_lossless(...)`)
  - Added adaptive cap for RGB conversion worker count:
    - hard cap: `kMaxRgbThreads = 8`
    - row granularity gate: `kMinRowsPerTask = 128`
    - pixel granularity gate: `kMinPixelsPerTask = 200000`
  - Avoided over-sharding for small/medium frames by reducing requested worker count.
  - Reduced async launch overhead:
    - launch `std::async` only for worker chunks `t=1..N-1`
    - process chunk `t=0` on caller thread
  - Existing deep counters remain active (`dispatch/kernel/wait`, rows/pixels).

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_plan3_mode01prep_opt_20260213_v2.csv`
- candidates:
  - `bench_results/tmp_ycocg_sched_opt_20260213.csv`
  - `bench_results/tmp_ycocg_sched_opt_20260213_rerun.csv`
  - `bench_results/tmp_ycocg_sched_opt_20260213_runs5.csv` (promoted)

### Result Summary (promoted runs=5)
- Compression invariants:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Decode performance vs baseline:
  - `hkn_dec_ms: 13.566274 -> 9.583383` (`-3.982891`, ~29.4% faster)
  - `hkn_dec_ycocg_to_rgb_ms: 5.436907 -> 1.155264` (`-4.281643`)
  - `ycocg dispatch/kernel/wait` now observable as:
    - `0.530 / 2.053 / 0.054 ms` (median over fixed-6)

Interpretation:
- The main win came from reducing scheduling overhead, not filter/reconstruct math changes.
- This confirms decode-side thread sharding policy as a high-ROI optimization lever.

### Current Promoted Baseline For Next Work
- Use this CSV as baseline for subsequent A/B:
  - `bench_results/tmp_ycocg_sched_opt_20260213_runs5.csv`

## 2026-02-13: Sequential Next-Move Implementation (Step1/2/3)

### Objective
- Implement the next three optimization targets in order:
  1. natural route `mode2` global-chain LZ micro-optimization
  2. `filter_lo` rANS decode loop-call overhead reduction
  3. decode plane scheduling wait reduction
- Keep bitstream format and compression behavior unchanged.

### Implementation
- `src/codec/lossless_natural_route.h`
  - `compress_global_chain_lz(...)`:
    - strengthened output reserve sizing for literal-heavy worst case.
    - factored match-length extension into a tighter pointer-based loop.
    - switched MATCH token emission to contiguous `resize + direct write`.
  - Search order / accept condition / tie-break were unchanged.
- `src/entropy/nyans_p/rans_flat_interleaved.h`
  - Added bulk decode helpers:
    - `decode_symbols(...)`
    - `decode_symbols_lut(...)`
  - These decode into caller-provided output buffers in one pass.
- `src/codec/decode.h`
  - `decode_byte_stream(...)` and `decode_byte_stream_shared_lz(...)` now use bulk decode helpers.
  - In `decode_color_lossless(...)` plane decode parallel path:
    - launch async for `Co` / `Cg`
    - run `Y` plane on caller thread
    - wait only for remaining futures
  - This preserves 3-plane parallel work while reducing pure idle wait in caller.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_ycocg_sched_opt_20260213_runs5.csv`
- candidates:
  - `bench_results/tmp_seq_opt_step123_20260213.csv`
  - `bench_results/tmp_seq_opt_step123_20260213_rerun.csv`
  - `bench_results/tmp_seq_opt_step123_20260213_runs5.csv`

### Result Summary (runs=5 vs promoted baseline)
- Compression invariants:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Stage-level movement:
  - `hkn_enc_plane_route_natural_candidate_ms: 35.142984 -> 34.874849` (`-0.268135`)
  - `hkn_enc_route_nat_mode2_ms: 29.533722 -> 28.380656` (`-1.153066`)
  - `hkn_dec_plane_wait_ms: 8.047220 -> 0.085119` (`-7.962101`)
- Wall decode movement:
  - `hkn_dec_ms: 9.583383 -> 8.324305` (`-1.259078`)
- Wall encode note:
  - fixed-6 median encode wall was higher in this run set (`99.472817 -> 123.392145`),
    while route-natural internals were improved or near-neutral.
  - Large-image host variance remained significant; this step is kept as implemented
    and should be re-judged with additional reruns when promoting a new baseline.

## 2026-02-13: Step Isolation (Step1 / Step2 / Step3)

### Objective
- Identify which step causes wall-time instability by toggling each step independently
  under a single binary.
- Keep size behavior unchanged.

### Implementation
- `src/codec/decode.h`
  - Added runtime flags:
    - `HKN_DECODE_BULK_RANS` (step2 toggle, default `on`)
    - `HKN_DECODE_PLANE_CALLER_Y` (step3 toggle, default `off`)
  - Reintroduced fallback paths so step2/step3 can be compared in-process.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts (runs=5, warmup=1)
- baseline:
  - `bench_results/tmp_ycocg_sched_opt_20260213_runs5.csv`
- step1 only (`BULK_RANS=0`, `PLANE_CALLER_Y=0`):
  - `bench_results/tmp_isolate_step1_only_20260213_runs5.csv`
- step1+2 (`BULK_RANS=1`, `PLANE_CALLER_Y=0`):
  - `bench_results/tmp_isolate_step12_20260213_runs5.csv`
- step1+2+3 (`BULK_RANS=1`, `PLANE_CALLER_Y=1`):
  - `bench_results/tmp_isolate_step123_20260213_runs5.csv`
- default (after changing step3 default to off):
  - `bench_results/tmp_isolate_default_step12_20260213_runs5.csv`

### Result Summary (median deltas vs baseline)
- Compression invariants were preserved in all cases:
  - `median PNG/HKN = 0.2610`
  - `total HKN bytes = 2,977,544`
- Wall-time comparison:
  - step1 only:
    - `hkn_enc_ms: 99.472817 -> 99.206570` (`-0.266247`)
    - `hkn_dec_ms: 9.583383 -> 9.066595` (`-0.516788`)
  - step1+2:
    - `hkn_enc_ms: 99.472817 -> 101.080500` (`+1.607683`)
    - `hkn_dec_ms: 9.583383 -> 8.577956` (`-1.005427`)
  - step1+2+3:
    - `hkn_enc_ms: 99.472817 -> 109.149485` (`+9.676669`)
    - `hkn_dec_ms: 9.583383 -> 8.691541` (`-0.891842`)

Interpretation:
- The large encode-wall regression appears only when step3 (`PLANE_CALLER_Y`) is enabled.
- step1 and step2 are safe to keep enabled for the current baseline strategy.
- Therefore step3 remains implemented but is now default-off (opt-in via env) until a
  stronger scheduler design removes the encode-side instability.

## 2026-02-13: `plane_reconstruct` DCT Row-Kernel Fast Path

### Objective
- Reduce decode-side `plane_reconstruct` cost without format or size changes.
- Keep COPY/TM4 behavior identical and optimize only DCT residual application path.

### Implementation
- `src/codec/lossless_plane_decode_core.h`
  - DCT reconstruction loop now uses row-kernel fast paths by `ftype` (0..5).
  - Added fast guard (`residual_idx + 8 <= residual_size`) and kept malformed-stream
    slow fallback for exact existing error accounting.
  - `ftype=0` (None predictor) now applies 8 residuals via direct memcpy.
  - `ftype=1/2/3/4/5` now run specialized row loops without per-pixel predictor switch.
  - Existing residual consumed/missing counters are preserved.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_isolate_default_step12_20260213_runs5.csv`
- candidates:
  - `bench_results/tmp_decode_recon_kernel_opt_20260213_runs5.csv`
  - `bench_results/tmp_decode_recon_kernel_opt_threadbudget_20260213_runs5_rerun.csv`

### Result Summary
- Compression invariants preserved:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Stable decode-stage improvement:
  - `plane_reconstruct` reduced from baseline class (`~5.48 ms`) to `~3.39-3.45 ms`
    in repeated runs.
- Wall decode moved in the expected direction:
  - typical runs observed `hkn_dec_ms` in `~7.6-8.3 ms` class vs baseline `8.626 ms`.
- Remaining decode wall bottleneck is now primarily scheduler wait (`plane_wait`) and
  `filter_lo` (`decode_rans`) rather than reconstruct math itself.

## 2026-02-13: Batch Thread-Budget Opt-in + CDF Lifecycle Cleanup

### Objective
- Improve batch-processing operability (outer image-level parallelism) while keeping
  default single-process behavior unchanged.
- Remove repeated dynamic CDF allocations that were not explicitly released in hot paths.

### Implementation
- `src/platform/thread_budget.h`
  - Added opt-in batch knobs:
    - `HAKONYANS_AUTO_INNER_THREADS=1`
    - `HAKONYANS_OUTER_WORKERS=<N>`
    - `HAKONYANS_INNER_THREADS_CAP=<N>` (optional)
  - Default behavior remains unchanged unless opt-in flag is enabled.
- `src/codec/decode.h`
  - Added explicit `CDFBuilder::cleanup(cdf)` in dynamic CDF decode paths:
    - `decode_stream(...)`
    - `decode_stream_parallel(...)`
    - `decode_byte_stream(...)`
- `src/codec/byte_stream_encoder.h`
  - Added explicit cleanup in `encode_byte_stream(...)`.
- `src/codec/encode.h`
  - Reworked token-stream encode callsites to hold CDF objects and release them
    explicitly after use (`dc/ac band paths`).

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Notes
- These changes do not alter bitstream format or compression decisions.
- Batch-thread knobs are operational controls for throughput tuning under outer-worker
  parallelism and are intentionally opt-in.

## 2026-02-13: `filter_lo` CDF Fast Path + LUT Table Reuse

### Objective
- Reduce `plane_filter_lo` rANS decode overhead in `decode_byte_stream(...)` without
  changing format, compression ratio, or decode output.

### Implementation
- `src/codec/decode.h`
  - Added `try_build_cdf_from_serialized_freq(...)`:
    - directly maps serialized frequency table to `CDFTable` when valid
      (`sum==RANS_TOTAL`, all `freq>0`, alphabet `<=256`).
    - keeps existing `CDFBuilder().build_from_freq(...)` path as fallback.
  - Added `build_simd_table_inplace(...)`:
    - fills a thread-local `SIMDDecodeTable` in place.
    - removes per-call LUT object allocation for the `count>=128` decode path.
  - Updated `decode_byte_stream(...)`:
    - uses direct CDF fast path when possible.
    - uses thread-local LUT table for `decode_symbols_lut(...)` /
      `decode_symbol_lut(...)`.
    - calls `CDFBuilder::cleanup(cdf)` only when dynamic fallback builder is used.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_isolate_default_step12_20260213_runs5.csv`
- candidate:
  - `bench_results/tmp_filterlo_cdf_fastpath_20260213_runs5.csv`

### Result Summary
- Compression invariants preserved:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Stage-level decode movement (baseline -> candidate):
  - `hkn_dec_ms`: `8.626 -> 8.010` (`-7.15%`)
  - `hkn_dec_filter_lo_decode_rans_ms`: `5.180 -> 4.896` (`-5.48%`)
- The gain is consistent with reducing per-stream CDF/LUT setup overhead in the
  `filter_lo` hot path.

## 2026-02-13: Caller-Y Decode Path Stats Isolation Fix (Experimental)

### Objective
- Keep `HKN_DECODE_PLANE_CALLER_Y=1` experimentation debuggable by avoiding
  main-thread stats reset during caller-executed Y-plane decode.

### Implementation
- `src/codec/decode.h`
  - `run_plane_task(...)` now accepts `reset_task_stats`:
    - async worker tasks (`Co`, `Cg`) use `true` (reset + capture local stats).
    - caller-thread Y task uses `false` (no reset of top-level stats object).
  - In caller-Y path, removed `accumulate_from(y_res.stats)` because Y-plane stats are
    already accumulated on the main thread.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_filterlo_cdf_fastpath_20260213_runs5.csv`
- caller-Y checks:
  - `bench_results/tmp_decode_plane_caller_y_recheck_20260213_runs5.csv`
  - `bench_results/tmp_decode_plane_caller_y_fixstats_20260213_runs5.csv`

### Result Summary
- Compression invariants preserved:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Debug counters are now coherent under caller-Y mode (scheduler/deep counters no
  longer get reset by the Y task running in caller thread).
- Caller-Y remains **opt-in / default-off**:
  - wall-clock gain was not stable enough to promote default behavior in this pass.

## 2026-02-13: Decode Worker-Pool Path + Mode3/4 Contiguous Writes + Batch Indicators

### Objective
- Reduce decode-side scheduler overhead (`dispatch/wait`) by removing repeated
  `std::async` thread creation in hot paths.
- Reduce `filter_lo` mode3/mode4 byte materialization overhead by avoiding
  per-byte `push_back`.
- Improve benchmark observability for batch workloads with explicit
  `images/s` and `cpu/wall` indicators.

### Implementation
- `src/codec/decode.h`
  - Added fixed decode worker pool (`decode_worker_pool()`) based on
    `thread_budget::max_threads(8)`.
  - In `decode_color_lossless(...)`:
    - replaced 3-plane decode `std::async` launches with pool `submit(...)`.
    - replaced YCoCg row-task `std::async` launches with pool `submit(...)`.
  - Kept existing thread-budget token gating and parallel counters unchanged.
- `src/codec/lossless_filter_lo_decode.h`
  - mode3 (`lo_mode==3`):
    - switched residual materialization from `reserve+push_back` to
      `assign(raw_count,0)` + contiguous indexed writes.
    - specialized predictor loops (`p=1/2/3/default`) to avoid per-byte
      branch dispatch.
  - mode4 (`lo_mode==4`):
    - switched from `push_back` fill to contiguous `assign(raw_count,0)` +
      row-wise indexed copy (`memcpy` for context slices).
- `bench/bench_png_compare.cpp`
  - Added batch indicators in CSV and console:
    - `hkn_enc_images_per_s`, `hkn_dec_images_per_s`,
      `png_enc_images_per_s`, `png_dec_images_per_s`
    - `hkn_enc_cpu_over_wall`, `hkn_dec_cpu_over_wall`
  - Added `=== Batch Indicators (median per image) ===` console section.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_filterlo_cdf_fastpath_20260213_runs5.csv`
- candidates:
  - `bench_results/tmp_step_next_pool_batch_mode34_20260213_runs5.csv`
  - `bench_results/tmp_step_next_pool_batch_mode34_20260213_runs5_rerun.csv`

### Result Summary (rerun promoted)
- Compression invariants preserved:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Median deltas vs baseline:
  - `hkn_dec_ms`: `8.010 -> 6.186` (`-22.8%`)
  - `hkn_dec_plane_filter_lo_ms`: `5.628 -> 4.317` (`-23.3%`)
  - `hkn_dec_plane_wait_ms`: `6.482 -> 5.272` (`-18.7%`)
  - `hkn_dec_plane_reconstruct_ms`: `3.444 -> 3.345` (`-2.9%`)
- Batch indicators (rerun):
  - `images/s Dec HKN/PNG`: `161.658 / 158.025` (`HKN/PNG = 1.023`)
  - `cpu/wall Dec(HKN)`: `1.946`

Interpretation:
- Fixed worker-pool scheduling gave a stable decode-wall reduction in repeated runs.
- Mode3/4 contiguous writes reduced allocation/write overhead in `filter_lo`.
- Added batch indicators now expose throughput/cpu-utilization trends directly in
  both CSV and terminal output.

## 2026-02-13: Encode Floor Step (BlockClass / LoStream / Route Low-Effect Skip)

### Objective
- Follow next encode-floor priority:
  1. `plane_block_class` loop cost reduction
  2. `plane_lo_stream` mode2/LZ preprocess overhead reduction
  3. `plane_route_comp` low-effect path skip / scheduler overhead reduction

### Implementation
- `src/codec/lossless_block_classifier.h`
  - Added classifier worker pool (`ThreadPool`) and replaced eval-chunk
    `std::async` launches with pool `submit(...)`.
  - Reworked block scan:
    - removed per-block `sort(64)` for unique counting.
    - switched to bounded unique tracking (`<=9`) during block load.
    - kept decision-equivalent behavior for all `unique<=8` cases.
- `src/codec/lossless_filter_lo_codec.h`
  - Added lo-stream worker pool and replaced base/ctx parallel `std::async`
    launches with pool `submit(...)`.
  - Mode3 build:
    - precomputed DCT-row lengths per 8-row block row.
    - `preds.reserve(active_rows)`, `resids.resize(...)` + indexed writes.
    - added predictor-cost early break when current cost already exceeds best.
  - Mode4 build:
    - pre-reserved ctx buffers from row-lens histogram.
    - switched ctx fill to contiguous `resize+memcpy`.
    - added safe early-abort in sequential ctx encode when gate/best cannot be met.
- `src/codec/lossless_route_competition.h`
  - Added route-competition worker pool for screen/natural parallel section.
  - Added early return when both routes are prefiltered out
    (`!allow_screen_route && !try_natural_route`).
- `src/codec/encode.h`
  - conservative chroma route-policy path now reuses precomputed prefilter metrics
    inside `choose_best_tile(...)` lambda to avoid duplicate prefilter work.

### Validation
- Build: `cmake --build build -j`
- Tests: `cd build && ctest --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- baseline:
  - `bench_results/tmp_step_next_pool_batch_mode34_20260213_runs5_rerun.csv`
- candidates:
  - `bench_results/tmp_step_encode_floor123_20260213_runs5.csv`
  - `bench_results/tmp_step_encode_floor123_20260213_runs5_rerun.csv`
  - `bench_results/tmp_step_encode_floor123_20260213_runs5_rerun2.csv`

### Result Summary
- Compression invariants preserved in all runs:
  - `median PNG/HKN = 0.2610` unchanged
  - `total HKN bytes = 2,977,544` unchanged
- Stable stage-level encode improvements (rerun2 vs baseline):
  - `hkn_enc_plane_block_classify_ms`: `64.822 -> 54.448` (`-16.0%`)
  - `hkn_enc_plane_lo_stream_ms`: `58.615 -> 55.623` (`-5.1%`)
  - `hkn_enc_plane_route_ms`: `43.360 -> 41.755` (`-3.7%`)
  - `hkn_enc_plane_route_natural_candidate_ms`: `32.395 -> 31.781` (`-1.9%`)
- Decode side was non-regressive and slightly better in rerun2:
  - `hkn_dec_ms`: `6.186 -> 6.086` (`-1.6%`)
  - `hkn_dec_plane_filter_lo_ms`: `4.317 -> 4.117` (`-4.6%`)

Interpretation:
- Encode wall-clock remained host-noise sensitive across reruns, but hotspot stage
  counters improved consistently in all three runs.
- The implemented changes are low-risk and maintain size behavior while reducing
  persistent encode hotspot costs.

## 2026-02-13: Lossless Preset Lanes (`fast` / `balanced` / `max`)

### Objective
- Add explicit lossless speed/size lanes without format change.
- Keep existing behavior as default (`balanced`) for compatibility.
- Enable controlled A/B for future lane-specific tuning.

### Implementation
- `src/codec/encode.h`
  - Added `GrayscaleEncoder::LosslessPreset`:
    - `FAST`, `BALANCED`, `MAX`
  - Added `lossless_preset_name(...)` helper.
  - Added internal preset plan builder:
    - `fast`: route competition off (luma/chroma)
    - `balanced`: current env-driven policy (existing behavior)
    - `max`: route competition on for all planes (compression-first lane)
  - Extended APIs with preset argument (default `balanced`):
    - `encode_lossless(...)`
    - `encode_color_lossless(...)`
- `bench/bench_png_compare.cpp`
  - Added `--preset fast|balanced|max`.
  - Benchmark encode path now passes selected preset into
    `encode_color_lossless(...)`.
- `tests/test_lossless_round2.cpp`
  - Added preset compatibility/validity tests:
    - default output equals explicit `balanced`
    - `fast` and `max` both keep lossless roundtrip.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: PASS (includes new preset tests)

### Benchmark Smoke (runs=1, warmup=0)
- `bench_results/tmp_preset_balanced_smoke_20260213.csv`
- `bench_results/tmp_preset_fast_smoke_20260213.csv`
- `bench_results/tmp_preset_max_smoke_20260213.csv`
- Intent: verify `--preset` end-to-end wiring and lane behavior only
  (not promotion-quality numbers).

### Notes
- This step is API/policy scaffolding and does not change bitstream format.
- `balanced` remains the promotion baseline lane for Phase 9w gates.

## 2026-02-13: Preset Lane Comparison (`runs=3`, `warmup=1`)

### Objective
- Evaluate the new preset lanes under identical conditions.
- Use `balanced` as the reference lane for speed/size tradeoff decisions.

### Benchmark Artifacts
- `bench_results/phase9w_preset_balanced_20260213_runs3.csv`
- `bench_results/phase9w_preset_fast_20260213_runs3.csv`
- `bench_results/phase9w_preset_max_20260213_runs3.csv`

### Result Summary
| preset | total HKN bytes | median PNG/HKN | median Enc(ms) | median Dec(ms) | natural selected/candidates |
|---|---:|---:|---:|---:|---:|
| balanced | 2,977,544 | 0.2610 | 107.806 | 6.183 | 3/10 |
| fast | 3,043,572 | 0.2610 | 78.607 | 6.068 | 0/0 |
| max | 2,977,544 | 0.2610 | 116.973 | 6.122 | 3/12 |

Delta vs `balanced`:
- `fast`
  - `total HKN bytes`: `+66,028` (size regression)
  - `median Enc(ms)`: `-29.199` (faster)
  - `median Dec(ms)`: `-0.116` (slightly faster)
- `max`
  - `total HKN bytes`: `0` (size parity with balanced on fixed 6)
  - `median Enc(ms)`: `+9.166` (slower)
  - `median Dec(ms)`: `-0.062` (slightly faster)

Per-image size deltas (`fast - balanced`):
- `kodim01`: `+22,326`
- `hd_01`: `+43,702`
- others: `0`

### Decision
- Keep `balanced` as default/promoted lane.
- `fast` is useful as throughput-focused experimental lane, but is outside
  non-regression size gate on fixed 6.
- `max` currently provides no size gain over `balanced` on fixed 6 while
  increasing encode wall time.

## 2026-02-13: `route_natural mode2` Deep Counters

### Objective
- Add deeper observability inside `mode2` global-chain LZ before the next
  optimization step and external consultation.

### Implementation
- `src/codec/lossless_mode_debug_stats.h`
  - Added `mode2` LZ counters:
    - calls / src bytes / out bytes
    - match count / match bytes / literal bytes
    - chain steps
    - depth-limit hits
    - max-len(255) early hits
    - len=3 reject-by-distance count
- `src/codec/lossless_natural_route.h`
  - Added `detail::GlobalChainLzCounters`.
  - Instrumented `compress_global_chain_lz(...)` internals with the counters.
  - Wired counter accumulation for all mode2 execution paths:
    - pipeline mode2 async
    - mode1/mode2 parallel path
    - sequential mode2 path
- `bench/bench_png_compare.cpp`
  - Added new CSV columns for mode2 LZ counters.
  - Added stage-breakdown print lines under `nat_mode2`.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifact
- `bench_results/phase9w_mode2_counters_balanced_20260213_runs3.csv`

### Observation Snapshot (median over fixed 6)
- `nat_mode2_lz calls/src/out`: `1 / 3,253,248 / 764,271`
- `nat_mode2_lz match/literal bytes`: `3,203,428 / 159`
- `nat_mode2_lz matches`: `177,906`
- `nat_mode2_lz chain/depth/maxlen/len3rej`:
  `3,530,143 / 68,325 / 2,303 / 38,993`

Per-image highlights:
- `hd_01` and `nature_*` dominate mode2 cost with very large
  `chain_steps` and non-trivial `len3_reject_dist`.
- `kodim01` remains a high-compression, low-literal case (near-all match bytes).

## 2026-02-13: Box Definition - mode2 len3-distance Early Reject

### Objective
- Reduce `mode2` encode time by avoiding unnecessary `match_len_from(...)`
  calls for candidates that can never pass the `len=3` distance rule.

### Scope
- Target file:
  - `src/codec/lossless_natural_route.h`
    - `compress_global_chain_lz(...)` inner match-search loop
- Non-target:
  - bitstream format
  - route selection/bias policy
  - decode path

### Planned Change
- For candidates with `dist > min_dist_len3`:
  - if `pos+3 >= src_size`, `len==3` is guaranteed -> reject immediately
  - else check 4th byte (`s[ref_pos+3] != s[pos+3]`) and reject immediately
- Keep existing `len3_reject_dist` accounting semantics.

### Boundaries / Gate
- Lossless + format compatibility must be preserved.
- `ctest --test-dir build --output-on-failure`: `17/17 PASS`
- Fixed 6 (`bench_png_compare`, `--preset balanced`, `runs=3,warmup=1`):
  - `total HKN bytes` non-regression
  - `median PNG/HKN` non-regression
  - target: `hkn_enc_route_nat_mode2_ms` and
    `hkn_enc_plane_route_natural_candidate_ms` improve.

### Implementation
- `src/codec/lossless_natural_route.h`
  - Added `len=3` distance reject precheck before `match_len_from(...)`:
    - if `dist > min_dist_len3` and `pos+3 >= src_size`, reject immediately.
    - if `dist > min_dist_len3` and 4th byte mismatches, reject immediately.
  - Kept existing `len3_reject_dist` accounting.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- Baseline:
  - `bench_results/phase9w_mode2_counters_balanced_20260213_runs3.csv`
- Candidate:
  - `bench_results/phase9w_mode2_len3_prereject_balanced_20260213_runs3.csv`
  - `bench_results/phase9w_mode2_len3_prereject_balanced_20260213_runs3_rerun.csv`

### Result Summary (vs baseline)
- Invariants:
  - `total HKN bytes`: unchanged (`2,977,544`)
  - `median PNG/HKN`: unchanged (`0.2610`)
- Stage counters:
  - `median route_natural(ms)`:
    - run1 `+0.965`
    - rerun `+0.676`
  - `median nat_mode2(ms)`:
    - run1 `+1.203`
    - rerun `+1.208`
- Wall-clock:
  - `median Enc(ms)` improved in both runs (`-0.806`, `-4.272`) but with
    host noise sensitivity.

### Decision
- **Not promoted as a clear mode2-stage win**.
- Keep as observed trial; next step should move to the stronger proposal:
  `match_len_from` low-level speedup (`XOR + ctz` style counting).
- Archived as no-go:
  - `docs/archive/2026-02-13_mode2_len3_prereject_nogo.md`
- Implementation is reverted from mainline before proceeding to next step.

## 2026-02-13: mode2 `match_len_from` XOR/ctz Fast Count

### Objective
- Reduce `mode2` inner-loop cost per `chain_step` by accelerating first-mismatch
  detection in `match_len_from(...)`.

### Implementation
- `src/codec/lossless_natural_route.h`
  - Updated `match_len_from(...)`:
    - when 8-byte block differs, use `va ^ vb` and trailing-zero byte count on
      little-endian targets to compute common prefix bytes in O(1).
    - non-little-endian fallback keeps byte-wise safe path.
  - No format/selection logic changes.

### Validation
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Result: `17/17 PASS`

### Benchmark Artifacts
- Baseline:
  - `bench_results/phase9w_mode2_counters_balanced_20260213_runs3.csv`
- Candidate:
  - `bench_results/phase9w_mode2_ctz_balanced_20260213_runs3.csv`
  - `bench_results/phase9w_mode2_ctz_balanced_20260213_runs3_rerun.csv`
  - `bench_results/phase9w_mode2_ctz_balanced_20260213_runs3_rerun2.csv`

### Result Summary (vs baseline)
- Invariants: all runs preserved
  - `total HKN bytes`: unchanged (`2,977,544`)
  - `median PNG/HKN`: unchanged (`0.2610`)
- Stage-level improvements were consistent across runs:
  - `median route_natural(ms)`: `-3.851 / -3.534 / -2.764`
  - `median nat_mode2(ms)`: `-3.380 / -3.380 / -2.367`
  - `median plane_route_comp(ms)`: `-4.544 / -4.801 / -3.758`
- Encode wall improved in all runs:
  - `median Enc(ms)`: `-22.308 / -6.350 / -23.091`
- Decode wall showed host-noise-scale variance (encode-only change):
  - `median Dec(ms)`: `-0.056 / +0.155 / +0.174`

### Decision
- Keep this optimization as the next promoted encode-step candidate:
  stage-target metrics (`mode2` / `route_natural`) improved consistently with
  size invariants preserved.
