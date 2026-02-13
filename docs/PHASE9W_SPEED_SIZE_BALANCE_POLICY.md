# Phase 9w: Speed/Size Balance Policy (Fixed)

## Scope
This policy defines the default go/no-go criteria for lossless optimization work in Phase 9w.

Target:
- `bench_png_compare` fixed 6 images
- HKN lossless path (`encode/decode`, route compete, filter_lo, reconstruct)

## North Star
Optimize on the Pareto frontier of:
- compression (`PNG_bytes / HKN_bytes`)
- wall-clock speed (`Enc(ms)`, `Dec(ms)`)

Do not optimize a single axis in isolation.

## Lossless Preset Lanes (Fixed API)
Lossless encode now has three explicit lanes for speed/size trade-off experiments:
1. `fast`: maximum-throughput lane.
   - default: route competition off (luma/chroma)
   - optional `fast_nat` mode (env opt-in):
     - `HKN_FAST_ROUTE_COMPETE=1` (enable luma route competition)
     - `HKN_FAST_ROUTE_COMPETE_CHROMA=1` (optional chroma route competition)
     - `HKN_FAST_LZ_NICE_LENGTH` (mode2 nice_length override for fast lane)
     - `HKN_FAST_LZ_MATCH_STRATEGY` (`0=greedy`, `1=lazy1`, default `0`)
2. `balanced`: default lane, keeps current promoted behavior and env-policy gates.
3. `max`: route competition on for all planes (including photo chroma) for compression-first exploration.
   - mode2 match strategy can be controlled by:
     - `HKN_MAX_LZ_MATCH_STRATEGY` (`0=greedy`, `1=lazy1`, `2=optparse_dp`, default `1`)
   - `optparse_dp` policy (max lane only):
     - keep token format unchanged (`litrun` + `match(len,dist)`)
     - balanced/fast must not route into DP path
     - fallback to existing strategy when DP guard fails (`memcap`, `alloc`, `unreachable`)
   - strategy2 (`optparse_dp`) gate knobs:
     - `HKN_LZ_OPTPARSE_PROBE_SRC_MAX` (default `2097152`)
     - `HKN_LZ_OPTPARSE_PROBE_RATIO_MIN` (default `20` permille)
     - `HKN_LZ_OPTPARSE_PROBE_RATIO_MAX` (default `120` permille)
     - `HKN_LZ_OPTPARSE_MIN_GAIN_BYTES` (default `1024`)
     - `HKN_LZ_OPTPARSE_MAX_MATCHES` (default `1`)
     - `HKN_LZ_OPTPARSE_LIT_MAX` (default `32`)

Rules:
- `balanced` is the compatibility anchor and baseline for day-to-day promotion.
- `fast`/`max` are opt-in experiment lanes and must be compared against `balanced`.
- All lanes must remain format-compatible and lossless.
- `fast_nat` is not a separate preset value; it is `--preset fast` + env opt-in.

## Fixed Metrics
Use these metrics only:
1. `median(PNG_bytes/HKN_bytes)` over fixed 6 images
2. total `HKN_bytes` over fixed 6 images
3. median `Enc(ms)` wall-clock (HKN)
4. median `Dec(ms)` wall-clock (HKN)
5. stage breakdown as diagnostic (`cpu_sum` counters)

Note:
- Stage counters are CPU-time sums and can exceed wall time under parallel execution.
- Go/no-go decisions must use wall-clock metrics, not CPU-sum.

## PNG-Anchored Absolute Targets (2026-02-13)
Reference dataset/command:
- fixed 6 images
- `./build/bench_png_compare --runs 5 --warmup 1 --out bench_results/tmp_isolate_default_step12_20260213_runs5.csv`

Absolute target values are pinned to current PNG measurements:
1. median `Enc(ms)` target: `<= 108.231`
2. median `Dec(ms)` target: `<= 6.409`
3. total `HKN_bytes` target: `<= 2,864,560`
4. median `PNG/HKN` target: `>= 1.0000`

Use these as milestone goals (PNG parity), while keeping the baseline non-regression
gate below for day-to-day landing safety.

## Fixed Acceptance Gate (default)
All conditions must pass to promote as mainline tuning:
1. `ctest --test-dir build --output-on-failure` is `17/17 PASS`
2. `median(PNG/HKN)` is non-regression vs baseline (`new >= baseline`)
3. total `HKN_bytes` is non-regression vs baseline (`new <= baseline`)
4. median `Enc(ms)` is non-regression vs baseline (`new <= baseline`)
5. median `Dec(ms)` is non-regression vs baseline (`new <= baseline`)

If any gate fails:
- keep as experiment branch/result
- do not replace current baseline

## Fixed Benchmark Procedure
1. Run benchmark (3-run median, 1 warmup):

```bash
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --preset balanced \
  --out bench_results/phase9w_speed_stage_profile.csv
```

2. Compare against baseline:

```bash
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --preset balanced \
  --baseline bench_results/phase9w_current.csv \
  --out bench_results/phase9w_speed_stage_profile_ab.csv
```

3. Save both CSVs and report:
- pass/fail for each gate
- key deltas (`median PNG/HKN`, total bytes, Enc wall, Dec wall)

4. Max-lane DP (strategy=2) promotion check:

```bash
HAKONYANS_THREADS=1 taskset -c 0 \
HKN_MAX_LZ_MATCH_STRATEGY=2 \
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --preset max \
  --out bench_results/phase9w_max_optparse_dp.csv
```

DP-specific acceptance:
- `ctest` pass
- `max` lane total bytes improves vs max-lane baseline
- `balanced`/`fast` unchanged behavior (no regression)

## Parallelization Rules
1. Prefer coarse-grain parallelism first:
- color plane parallelism
- route candidate parallelism

2. Add size-based gates before spawning async work:
- avoid parallel overhead on small streams/tiles

3. Avoid unbounded nested parallelism:
- cap by `hardware_concurrency`
- keep scheduling deterministic for benchmark reproducibility

4. Keep quality gates strict:
- no format changes
- no compression regression to buy speed

## Reporting Template
Use this short format in PR/commit notes:

```text
Gate:
- Tests: PASS/FAIL
- median PNG/HKN: new vs baseline
- total HKN bytes: new vs baseline
- Enc wall(ms): new vs baseline
- Dec wall(ms): new vs baseline

Hotspots (cpu_sum median):
- encode: ...
- decode: ...
```
