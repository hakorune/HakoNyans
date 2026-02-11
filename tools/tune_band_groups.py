#!/usr/bin/env python3
"""Brute-force tuner for band-group CDF split points.

This script recompiles with different compile-time band split values:
  - HAKONYANS_BAND_LOW_END
  - HAKONYANS_BAND_MID_END
and measures:
  - lossy bytes from bench_bit_accounting
  - decode latency from bench_decode

Two-pass search:
  1) coarse pass over all candidates
  2) re-measure top-K candidates with more decode runs
"""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import re
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Iterable

TOTAL_RE = re.compile(r"^TOTAL\s+(\d+)\s+", re.MULTILINE)
DECODE_MS_RE = re.compile(r"Average Decode Time:\s*([-+]?\d+(?:\.\d+)?)\s*ms")


@dataclass
class Case:
    image: str
    quality: int


@dataclass
class Result:
    low_end: int
    mid_end: int
    total_bytes: int
    decode_ms: float
    decode_samples: list[float]


def run_cmd(cmd: list[str], cwd: pathlib.Path, quiet: bool = False) -> str:
    proc = subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=True)
    if proc.returncode != 0:
        if not quiet:
            sys.stderr.write(f"Command failed ({proc.returncode}): {' '.join(cmd)}\n")
            if proc.stdout:
                sys.stderr.write("--- stdout ---\n" + proc.stdout + "\n")
            if proc.stderr:
                sys.stderr.write("--- stderr ---\n" + proc.stderr + "\n")
        raise RuntimeError(f"command failed: {' '.join(cmd)}")
    return proc.stdout


def configure_build(repo: pathlib.Path, build_dir: pathlib.Path, low_end: int, mid_end: int, cmake_args: list[str]) -> None:
    cmd = [
        "cmake",
        "-S", str(repo),
        "-B", str(build_dir),
        "-DHAKONYANS_BUILD_TESTS=OFF",
        "-DHAKONYANS_BUILD_TOOLS=OFF",
        "-DHAKONYANS_BUILD_BENCH=ON",
        f"-DHAKONYANS_BAND_LOW_END={low_end}",
        f"-DHAKONYANS_BAND_MID_END={mid_end}",
        *cmake_args,
    ]
    run_cmd(cmd, repo)


def build_targets(repo: pathlib.Path, build_dir: pathlib.Path, jobs: int) -> None:
    cmd = [
        "cmake", "--build", str(build_dir),
        "-j", str(jobs),
        "--target", "bench_bit_accounting", "bench_decode",
    ]
    run_cmd(cmd, repo)


def measure_total_bytes(build_dir: pathlib.Path, cases: list[Case]) -> int:
    bench = build_dir / "bench_bit_accounting"
    total = 0
    for case in cases:
        cmd = [
            str(bench),
            case.image,
            "--lossy",
            "--quality",
            str(case.quality),
        ]
        out = run_cmd(cmd, build_dir)
        m = TOTAL_RE.search(out)
        if not m:
            raise RuntimeError(f"TOTAL not found in bench_bit_accounting output for {case.image} Q{case.quality}")
        total += int(m.group(1))
    return total


def measure_decode_ms(build_dir: pathlib.Path, runs: int) -> tuple[float, list[float]]:
    bench = build_dir / "bench_decode"
    values: list[float] = []
    attempts = 0
    max_attempts = max(3, runs * 4)

    while len(values) < runs and attempts < max_attempts:
        attempts += 1
        out = run_cmd([str(bench)], build_dir)
        m = DECODE_MS_RE.search(out)
        if not m:
            continue
        ms = float(m.group(1))
        if math.isfinite(ms) and ms > 0.0:
            values.append(ms)

    if len(values) < runs:
        raise RuntimeError(f"Failed to collect decode runs: got {len(values)} / {runs}")

    return statistics.median(values), values


def evaluate_candidate(
    repo: pathlib.Path,
    build_dir: pathlib.Path,
    low_end: int,
    mid_end: int,
    cases: list[Case],
    decode_runs: int,
    jobs: int,
    cmake_args: list[str],
) -> Result:
    configure_build(repo, build_dir, low_end, mid_end, cmake_args)
    build_targets(repo, build_dir, jobs)
    total_bytes = measure_total_bytes(build_dir, cases)
    decode_ms, samples = measure_decode_ms(build_dir, decode_runs)
    return Result(
        low_end=low_end,
        mid_end=mid_end,
        total_bytes=total_bytes,
        decode_ms=decode_ms,
        decode_samples=samples,
    )


def generate_candidates(low_min: int, low_max: int, mid_min: int, mid_max: int, step: int) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for low in range(low_min, low_max + 1, step):
        start_mid = max(mid_min, low + 1)
        for mid in range(start_mid, mid_max + 1, step):
            if mid <= low:
                continue
            if low < 1 or low > 61 or mid < 2 or mid > 62:
                continue
            out.append((low, mid))
    return out


def sort_key(res: Result, baseline_bytes: int, baseline_decode_ms: float, max_decode_ratio: float, penalty_weight: float) -> tuple[float, float]:
    size_ratio = res.total_bytes / baseline_bytes
    decode_ratio = res.decode_ms / baseline_decode_ms
    penalty = max(0.0, decode_ratio - max_decode_ratio) * penalty_weight
    score = size_ratio + penalty
    return (score, size_ratio)


def print_result_row(r: Result, baseline: Result, max_decode_ratio: float) -> None:
    size_pct = (r.total_bytes / baseline.total_bytes - 1.0) * 100.0
    dec_pct = (r.decode_ms / baseline.decode_ms - 1.0) * 100.0
    ok = "OK" if (r.decode_ms / baseline.decode_ms) <= max_decode_ratio else "SLOW"
    print(
        f"low={r.low_end:2d} mid={r.mid_end:2d} | "
        f"bytes={r.total_bytes:9d} ({size_pct:+6.2f}%) | "
        f"decode={r.decode_ms:7.3f}ms ({dec_pct:+6.2f}%) | {ok}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Tune band-group AC split points")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--build-dir", default="build_band_tune", help="Build directory used for tuning")
    parser.add_argument("--image", action="append", default=[], help="Input image path for bit accounting (repeatable)")
    parser.add_argument("--quality", action="append", type=int, default=[], help="Lossy quality for each image evaluation (repeatable)")
    parser.add_argument("--low-min", type=int, default=8)
    parser.add_argument("--low-max", type=int, default=24)
    parser.add_argument("--mid-min", type=int, default=24)
    parser.add_argument("--mid-max", type=int, default=48)
    parser.add_argument("--step", type=int, default=2)
    parser.add_argument("--topk", type=int, default=6)
    parser.add_argument("--decode-runs-pass1", type=int, default=1)
    parser.add_argument("--decode-runs-pass2", type=int, default=3)
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 1))
    parser.add_argument("--max-decode-ratio", type=float, default=1.05,
                        help="Allowed decode slowdown ratio vs baseline (e.g. 1.05 = +5%%)")
    parser.add_argument("--penalty-weight", type=float, default=8.0,
                        help="Penalty for decode slowdown beyond max-decode-ratio")
    parser.add_argument("--json-out", default="", help="Optional JSON output path")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Extra cmake configure arg (repeatable)")
    parser.add_argument("--baseline-low", type=int, default=15)
    parser.add_argument("--baseline-mid", type=int, default=31)
    args = parser.parse_args()

    repo = pathlib.Path(args.repo).resolve()
    build_dir = pathlib.Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = repo / build_dir

    images = args.image if args.image else ["../test_images/photo/nature_01.ppm"]
    qualities = args.quality if args.quality else [50]

    cases: list[Case] = []
    for image in images:
        for q in qualities:
            cases.append(Case(image=image, quality=q))

    candidates = generate_candidates(args.low_min, args.low_max, args.mid_min, args.mid_max, args.step)
    if not candidates:
        raise SystemExit("No candidates generated. Check low/mid ranges.")

    baseline_pair = (args.baseline_low, args.baseline_mid)
    if baseline_pair not in candidates:
        candidates.insert(0, baseline_pair)

    print("=== Band-group Split Tuner ===")
    print(f"repo: {repo}")
    print(f"build_dir: {build_dir}")
    print(f"cases: {[(c.image, c.quality) for c in cases]}")
    print(f"candidates: {len(candidates)}")
    print(f"baseline: low={baseline_pair[0]}, mid={baseline_pair[1]}")
    print()

    pass1_results: list[Result] = []
    t0 = time.time()
    for idx, (low, mid) in enumerate(candidates, start=1):
        print(f"[pass1 {idx}/{len(candidates)}] low={low} mid={mid} ...", flush=True)
        res = evaluate_candidate(
            repo=repo,
            build_dir=build_dir,
            low_end=low,
            mid_end=mid,
            cases=cases,
            decode_runs=args.decode_runs_pass1,
            jobs=args.jobs,
            cmake_args=args.cmake_arg,
        )
        pass1_results.append(res)
        print(f"  -> bytes={res.total_bytes}, decode={res.decode_ms:.3f}ms")

    baseline = next((r for r in pass1_results if (r.low_end, r.mid_end) == baseline_pair), None)
    if baseline is None:
        raise RuntimeError("Baseline candidate was not measured")

    ranked_pass1 = sorted(
        pass1_results,
        key=lambda r: sort_key(r, baseline.total_bytes, baseline.decode_ms, args.max_decode_ratio, args.penalty_weight),
    )
    selected = ranked_pass1[: max(1, args.topk)]

    print("\n=== Pass1 Top Candidates ===")
    for r in selected:
        print_result_row(r, baseline, args.max_decode_ratio)

    pass2_results: list[Result] = []
    print("\n=== Pass2 Re-Measure ===")
    for idx, r in enumerate(selected, start=1):
        print(f"[pass2 {idx}/{len(selected)}] low={r.low_end} mid={r.mid_end} ...", flush=True)
        rr = evaluate_candidate(
            repo=repo,
            build_dir=build_dir,
            low_end=r.low_end,
            mid_end=r.mid_end,
            cases=cases,
            decode_runs=args.decode_runs_pass2,
            jobs=args.jobs,
            cmake_args=args.cmake_arg,
        )
        pass2_results.append(rr)
        print(f"  -> bytes={rr.total_bytes}, decode={rr.decode_ms:.3f}ms samples={rr.decode_samples}")

    # Ensure baseline exists in pass2 for precise deltas
    baseline2 = next((r for r in pass2_results if (r.low_end, r.mid_end) == baseline_pair), None)
    if baseline2 is None:
        baseline2 = evaluate_candidate(
            repo=repo,
            build_dir=build_dir,
            low_end=baseline_pair[0],
            mid_end=baseline_pair[1],
            cases=cases,
            decode_runs=args.decode_runs_pass2,
            jobs=args.jobs,
            cmake_args=args.cmake_arg,
        )
        pass2_results.append(baseline2)

    ranked_pass2 = sorted(
        pass2_results,
        key=lambda r: sort_key(r, baseline2.total_bytes, baseline2.decode_ms, args.max_decode_ratio, args.penalty_weight),
    )

    print("\n=== Final Ranking (Pass2) ===")
    for r in ranked_pass2:
        print_result_row(r, baseline2, args.max_decode_ratio)

    best = ranked_pass2[0]
    elapsed = time.time() - t0
    print("\n=== Recommended Split ===")
    print_result_row(best, baseline2, args.max_decode_ratio)
    print(f"Elapsed: {elapsed:.1f}s")

    if args.json_out:
        payload = {
            "baseline": baseline2.__dict__,
            "best": best.__dict__,
            "pass1": [r.__dict__ for r in pass1_results],
            "pass2": [r.__dict__ for r in pass2_results],
            "args": vars(args),
            "elapsed_sec": elapsed,
        }
        out_path = pathlib.Path(args.json_out)
        if not out_path.is_absolute():
            out_path = repo / out_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"JSON saved: {out_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
