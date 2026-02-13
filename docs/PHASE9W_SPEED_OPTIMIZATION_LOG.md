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
