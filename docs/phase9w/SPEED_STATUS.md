# Phase 9w Speed Status

Last updated: 2026-02-14 (post Trial G)

## Current Lane
- Default lane: `balanced`
- Size gate baseline (fixed 6):
  - `total HKN bytes = 2,977,544`
  - `median PNG/HKN = 0.2610`

## Current Working Baseline CSV
- `bench_results/phase9w_mode2_ctz_balanced_20260213_runs3_rerun2.csv`
- Single-core reference:
  - `bench_results/phase9w_singlecore_after_drift_fix_vs_singlecorebase_20260213_runs3.csv`
- Latest observation run (mode2 nice cutoff counters):
  - `bench_results/phase9w_singlecore_mode2_nice255_vs_lostreamlb_20260214_runs3.csv`

Rationale:
- Preserves size invariants.
- Shows consistent stage wins in `route_natural` / `nat_mode2` from the XOR+ctz step.
- `filter_lo` now has safe lower-bound skip for weak mode3/4 candidates.
- `mode2 nice_length` infrastructure is integrated with safe default (`255`).

## Latest Box Decisions
1. `mode2 len3-distance prereject`: no-go (archived and reverted)
- archive: `docs/archive/2026-02-13_mode2_len3_prereject_nogo.md`

2. `mode2 match_len XOR+ctz`: kept as promoted mode2 optimization candidate
- details: `docs/phase9w/logs/2026-02-13.md`

3. `mode2 len3 fast-path (4th-byte gate)`: kept as stage-level candidate, baseline promotion on hold
- size invariants preserved
- `nat_mode2` improved across reruns, wall-clock noisy
- details: `docs/phase9w/logs/2026-02-13.md`

4. `plane_lo_stream` pruning trial: no-go (reverted)
- details: `docs/phase9w/logs/2026-02-13.md`

5. `block_class copy-shortcut + palette extract reuse + mode2 feasibility skip`: hold candidate
- invariants preserved
- stage gains are mixed under host noise, baseline promotion on hold
- details: `docs/phase9w/logs/2026-02-13.md`

6. `mode2 pipeline feasibility gate`: no-go (reverted)
- invariants preserved but route wall-time regressed due reduced overlap
- archive: `docs/archive/2026-02-13_mode2_pipeline_limit_gating_nogo.md`

7. `block_class copy forced shortcut`: hold candidate
- invariants preserved
- `plane_block_classify` stage improved consistently, wall-clock was mixed across reruns
- observation counter `hkn_enc_class_copy_shortcut_selected` added to CSV
- details: `docs/phase9w/logs/2026-02-13.md`

8. `plane_lo_stream` mode-wise counters: completed and kept
- added `hkn_enc_lo_mode{2,3,4,5}_eval_ms` to CSV and stage summary.
- latest observation: mode2/mode3 dominate lo-stream compute.
- details: `docs/phase9w/logs/2026-02-13.md`

9. `TileLZ::compress` cost reduction + mode3 one-pass predictor cost: no-go (reverted)
- stage trend was partially positive, but wall-clock variance remained too high
  to justify promotion.
- reverted for now; keep observation-only state.
- details: `docs/phase9w/logs/2026-02-13.md`

10. `mode2` 4th-byte early-skip: no-go (reverted)
- size invariants preserved but encode speed signal was unstable across reruns.
- details: `docs/phase9w/logs/2026-02-13.md`

11. `mode2` counter/prune micro-optimizations: no-go (reverted)
- local-counter and best-len prune variants both kept size invariants.
- no stable end-to-end encode gain across reruns.
- details: `docs/phase9w/logs/2026-02-13.md`

12. `mode2` beat-offset prune: no-go (reverted)
- size invariants preserved.
- stage/wall encode regressed in trial measurements.
- details: `docs/phase9w/logs/2026-02-13.md`

13. `plane_lo_stream` mode3 branch-reduction rewrite: no-go (reverted)
- stage-local gain was observed, but single-core wall regressed.
- archive: `docs/archive/2026-02-14_lostream_mode3_branchcut_nogo.md`

14. `byte_stream_encoder` alloc/copy reduction: no-go (reverted)
- size invariants preserved; reruns did not show stable single-core gain.
- archive: `docs/archive/2026-02-14_bstream_allocopt_nogo.md`

15. `route_natural` cost-loop fast-abs substitution: no-go (reverted)
- size invariants preserved; route_comp and wall encode regressed.
- archive: `docs/archive/2026-02-14_routecost_fastabs_nogo.md`

16. `TileLZ::compress` head-init/literal-flush optimization: kept candidate
- size invariants preserved; single-core reruns showed net wall gain in 3/4 runs.
- details: `docs/phase9w/logs/2026-02-14.md`

17. `filter_lo` mode3/4 safe lower-bound skip + encode mode adoption CSV: kept
- size invariants preserved.
- fixed6 adoption showed every mode (0..5) still appears on at least one image,
  so static mode disable is on hold.
- details: `docs/phase9w/logs/2026-02-14.md`

18. `route_natural mode2` nice-length cutoff: infra kept, `nice=64` no-go
- `nice=64` reduced chain steps but violated size gate (+7,144 B total).
- default was reset to `nice=255`; behavior is baseline-equivalent unless env override.
- archive: `docs/archive/2026-02-14_mode2_nice64_nogo.md`
- details: `docs/phase9w/logs/2026-02-14.md`

19. `fast` lane binding for `mode2 nice_length`: kept (infra)
- `fast` preset now carries `natural_route_mode2_nice_length_override`
  via `HKN_FAST_LZ_NICE_LENGTH` (default `64`).
- current `fast` policy keeps route competition OFF, so default fast path
  does not execute natural route yet.
- details: `docs/phase9w/logs/2026-02-14.md`

## Single-Core Snapshot (`HAKONYANS_THREADS=1`)
- source: `bench_results/phase9w_singlecore_tilelz_compress_fast_vs_step2_20260214_runs3_rerun2.csv`
- median Enc(ms) HKN/PNG: `175.056 / 107.321` (`HKN/PNG=1.631`)
- median Dec(ms) HKN/PNG: `12.698 / 6.311` (`HKN/PNG=2.012`)
- median encode stage hotspots:
  - `plane_route_comp: 57.312 ms`
  - `plane_lo_stream: 56.434 ms`
  - `plane_block_class: 26.563 ms`

Interpretation:
- Multicore wall metrics are competitive, but per-core efficiency is still behind PNG.
- Next optimization should prioritize single-core hotspots.

## Next Tasks
1. Sweep `HKN_LZ_NICE_LENGTH` with strict size gate (`balanced`)
- target range: `128, 160, 192, 224, 255`
- promote only if fixed6 total bytes is non-worsening

2. Optional: fast route-competition opt-in experiment
- evaluate whether enabling fast-lane route competition plus
  `HKN_FAST_LZ_NICE_LENGTH` improves fast lane Pareto

3. Re-run fixed low-noise validation for each candidate
- fixed condition: `HAKONYANS_THREADS=1` + `taskset -c 0`, fixed6, `runs=3`, `warmup=1`.

4. Resume `plane_lo_stream` mode2 optimization after Trial F
- keep prior no-go constraints (`out_limit` path gating remains disabled)

5. Keep promote protocol and archive-by-default
- repeated fixed-condition reruns required for promotion.
- all no-go and hold outcomes must be recorded with CSV links.
