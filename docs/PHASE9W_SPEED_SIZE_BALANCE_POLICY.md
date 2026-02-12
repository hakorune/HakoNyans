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
  --out bench_results/phase9w_speed_stage_profile.csv
```

2. Compare against baseline:

```bash
./build/bench_png_compare \
  --runs 3 --warmup 1 \
  --baseline bench_results/phase9w_current.csv \
  --out bench_results/phase9w_speed_stage_profile_ab.csv
```

3. Save both CSVs and report:
- pass/fail for each gate
- key deltas (`median PNG/HKN`, total bytes, Enc wall, Dec wall)

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

