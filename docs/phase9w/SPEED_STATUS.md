# Phase 9w Speed Status

Last updated: 2026-02-13

## Current Lane
- Default lane: `balanced`
- Size gate baseline (fixed 6):
  - `total HKN bytes = 2,977,544`
  - `median PNG/HKN = 0.2610`

## Current Working Baseline CSV
- `bench_results/phase9w_mode2_ctz_balanced_20260213_runs3_rerun2.csv`

Rationale:
- Preserves size invariants.
- Shows consistent stage wins in `route_natural` / `nat_mode2` from the XOR+ctz step.

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

## Next Tasks
1. Reduce host-noise sensitivity for promote decisions
- use repeated fixed-condition reruns for route-level promotion

2. Continue high-ROI encode work in `plane_block_classify` / `route_natural`
- prioritize isolated single-change runs for clearer attribution

3. Isolate side-effects of copy forced shortcut
- re-run with current host lock conditions and verify whether `lo_stream` drift is noise

4. Keep archive-by-default
- all no-go and hold outcomes must be recorded with CSV links
