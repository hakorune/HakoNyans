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

Reliability features:
  - periodic checkpoint JSON writes
  - resume support from checkpoint
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
        out = run_cmd([
            str(bench),
            case.image,
            "--lossy",
            "--quality",
            str(case.quality),
        ], build_dir)
        m = TOTAL_RE.search(out)
        if not m:
            raise RuntimeError(f"TOTAL not found in bench_bit_accounting output for {case.image} Q{case.quality}")
        total += int(m.group(1))
    return total


def measure_decode_ms(build_dir: pathlib.Path, runs: int) -> tuple[float, list[float]]:
    bench = build_dir / "bench_decode"
    values: list[float] = []
    attempts = 0
    max_attempts = max(4, runs * 5)

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
            if low < 1 or low > 61 or mid < 2 or mid > 62:
                continue
            if mid <= low:
                continue
            out.append((low, mid))
    return out


def result_key(r: Result) -> tuple[int, int]:
    return (r.low_end, r.mid_end)


def result_to_dict(r: Result) -> dict:
    return {
        "low_end": r.low_end,
        "mid_end": r.mid_end,
        "total_bytes": r.total_bytes,
        "decode_ms": r.decode_ms,
        "decode_samples": r.decode_samples,
    }


def result_from_dict(d: dict) -> Result:
    return Result(
        low_end=int(d["low_end"]),
        mid_end=int(d["mid_end"]),
        total_bytes=int(d["total_bytes"]),
        decode_ms=float(d["decode_ms"]),
        decode_samples=[float(x) for x in d.get("decode_samples", [])],
    )


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


def save_json_atomic(path: pathlib.Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    tmp.replace(path)


def save_checkpoint(
    out_path: pathlib.Path | None,
    status: str,
    stage: str,
    args_obj: argparse.Namespace,
    elapsed_sec: float,
    pass1_results: list[Result],
    pass2_results: list[Result],
    baseline: Result | None = None,
    best: Result | None = None,
    error: str | None = None,
) -> None:
    if out_path is None:
        return
    payload = {
        "status": status,
        "stage": stage,
        "baseline": result_to_dict(baseline) if baseline else None,
        "best": result_to_dict(best) if best else None,
        "pass1": [result_to_dict(r) for r in pass1_results],
        "pass2": [result_to_dict(r) for r in pass2_results],
        "args": vars(args_obj),
        "elapsed_sec": elapsed_sec,
    }
    if error:
        payload["error"] = error
    save_json_atomic(out_path, payload)


def load_checkpoint(out_path: pathlib.Path) -> tuple[list[Result], list[Result]]:
    if not out_path.exists():
        return ([], [])
    data = json.loads(out_path.read_text(encoding="utf-8"))
    pass1 = [result_from_dict(d) for d in data.get("pass1", [])]
    pass2 = [result_from_dict(d) for d in data.get("pass2", [])]
    return (pass1, pass2)


def resolve_image_path(repo: pathlib.Path, image: str) -> str:
    p = pathlib.Path(image)
    if p.is_absolute():
        return str(p)
    candidate = (repo / p).resolve()
    if candidate.exists():
        return str(candidate)
    # Keep backward-compatible behavior for paths relative to build dir.
    return image


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
    parser.add_argument("--resume", action="store_true", help="Resume from existing --json-out checkpoint")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Extra cmake configure arg (repeatable)")
    parser.add_argument("--baseline-low", type=int, default=15)
    parser.add_argument("--baseline-mid", type=int, default=31)
    args = parser.parse_args()

    repo = pathlib.Path(args.repo).resolve()
    build_dir = pathlib.Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = repo / build_dir

    out_path: pathlib.Path | None = None
    if args.json_out:
        out_path = pathlib.Path(args.json_out)
        if not out_path.is_absolute():
            out_path = repo / out_path

    images = args.image if args.image else ["test_images/photo/nature_01.ppm"]
    qualities = args.quality if args.quality else [50]

    cases: list[Case] = []
    for image in images:
        resolved = resolve_image_path(repo, image)
        for q in qualities:
            cases.append(Case(image=resolved, quality=q))

    candidates = generate_candidates(args.low_min, args.low_max, args.mid_min, args.mid_max, args.step)
    if not candidates:
        raise SystemExit("No candidates generated. Check low/mid ranges.")

    baseline_pair = (args.baseline_low, args.baseline_mid)
    if baseline_pair not in candidates:
        candidates.insert(0, baseline_pair)

    pass1_results: list[Result] = []
    pass2_results: list[Result] = []
    if args.resume:
        if out_path is None:
            raise SystemExit("--resume requires --json-out")
        loaded_p1, loaded_p2 = load_checkpoint(out_path)
        candidate_set = set(candidates)
        pass1_results = [r for r in loaded_p1 if result_key(r) in candidate_set]
        pass2_results = [r for r in loaded_p2 if result_key(r) in candidate_set]

    print("=== Band-group Split Tuner ===")
    print(f"repo: {repo}")
    print(f"build_dir: {build_dir}")
    print(f"cases: {[(c.image, c.quality) for c in cases]}")
    print(f"candidates: {len(candidates)}")
    print(f"baseline: low={baseline_pair[0]}, mid={baseline_pair[1]}")
    if args.resume and out_path is not None:
        print(f"resume: {out_path} (pass1={len(pass1_results)}, pass2={len(pass2_results)})")
    print()

    t0 = time.time()
    try:
        p1_existing = {result_key(r) for r in pass1_results}
        for idx, (low, mid) in enumerate(candidates, start=1):
            if (low, mid) in p1_existing:
                print(f"[pass1 {idx}/{len(candidates)}] low={low} mid={mid} ... skip(resume)")
                continue
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
            p1_existing.add((low, mid))
            print(f"  -> bytes={res.total_bytes}, decode={res.decode_ms:.3f}ms")
            save_checkpoint(
                out_path,
                status="running",
                stage=f"pass1 ({len(pass1_results)}/{len(candidates)})",
                args_obj=args,
                elapsed_sec=time.time() - t0,
                pass1_results=pass1_results,
                pass2_results=pass2_results,
            )

        baseline = next((r for r in pass1_results if result_key(r) == baseline_pair), None)
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

        p2_existing = {result_key(r): r for r in pass2_results}
        print("\n=== Pass2 Re-Measure ===")
        for idx, r in enumerate(selected, start=1):
            key = result_key(r)
            if key in p2_existing:
                rr = p2_existing[key]
                print(f"[pass2 {idx}/{len(selected)}] low={rr.low_end} mid={rr.mid_end} ... skip(resume)")
                continue
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
            p2_existing[key] = rr
            print(f"  -> bytes={rr.total_bytes}, decode={rr.decode_ms:.3f}ms samples={rr.decode_samples}")
            save_checkpoint(
                out_path,
                status="running",
                stage=f"pass2 ({len(pass2_results)}/{len(selected)})",
                args_obj=args,
                elapsed_sec=time.time() - t0,
                pass1_results=pass1_results,
                pass2_results=pass2_results,
                baseline=baseline,
            )

        baseline2 = next((r for r in pass2_results if result_key(r) == baseline_pair), None)
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

        save_checkpoint(
            out_path,
            status="completed",
            stage="done",
            args_obj=args,
            elapsed_sec=elapsed,
            pass1_results=pass1_results,
            pass2_results=pass2_results,
            baseline=baseline2,
            best=best,
        )
        if out_path is not None:
            print(f"JSON saved: {out_path}")
        return 0

    except KeyboardInterrupt:
        save_checkpoint(
            out_path,
            status="interrupted",
            stage="interrupted",
            args_obj=args,
            elapsed_sec=time.time() - t0,
            pass1_results=pass1_results,
            pass2_results=pass2_results,
        )
        print("Interrupted. Partial checkpoint saved.")
        return 130

    except Exception as exc:
        save_checkpoint(
            out_path,
            status="failed",
            stage="failed",
            args_obj=args,
            elapsed_sec=time.time() - t0,
            pass1_results=pass1_results,
            pass2_results=pass2_results,
            error=str(exc),
        )
        raise


if __name__ == "__main__":
    raise SystemExit(main())
