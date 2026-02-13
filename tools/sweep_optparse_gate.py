#!/usr/bin/env python3
import csv
import os
import statistics
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
BENCH = REPO / "build" / "bench_png_compare"
OUT_DIR = REPO / "bench_results"
OUT_DIR.mkdir(parents=True, exist_ok=True)


IMAGES = ["kodim01", "kodim02", "kodim03", "hd_01", "nature_01", "nature_02"]


def run_bench(env_extra, out_csv, runs=1, warmup=0):
    env = os.environ.copy()
    env.update(
        {
            "HAKONYANS_THREADS": "1",
            "HKN_MAX_LZ_MATCH_STRATEGY": "2",
            "OMP_NUM_THREADS": "1",
        }
    )
    env.update({k: str(v) for k, v in env_extra.items()})

    cmd = [
        "taskset",
        "-c",
        "0",
        str(BENCH),
        "--runs",
        str(runs),
        "--warmup",
        str(warmup),
        "--preset",
        "max",
        "--out",
        str(out_csv),
    ]
    subprocess.run(
        cmd,
        cwd=REPO,
        env=env,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )


def parse_metrics(csv_path):
    rows = []
    with open(csv_path, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            if row["image_name"] in IMAGES:
                rows.append(row)
    if len(rows) != 6:
        raise RuntimeError(f"Unexpected row count in {csv_path}: {len(rows)}")

    total_bytes = sum(int(r["hkn_bytes"]) for r in rows)
    enc_samples = sorted(float(r["hkn_enc_ms"]) for r in rows)
    dec_samples = sorted(float(r["hkn_dec_ms"]) for r in rows)
    png_over_hkn = sorted(float(r["png_over_hkn"]) for r in rows)

    def median(vals):
        return statistics.median(vals)

    kodim01 = next(r for r in rows if r["image_name"] == "kodim01")
    return {
        "total_hkn_bytes": total_bytes,
        "median_enc_ms": median(enc_samples),
        "median_dec_ms": median(dec_samples),
        "median_png_over_hkn": median(png_over_hkn),
        "kodim01_hkn_bytes": int(kodim01["hkn_bytes"]),
        "kodim01_enc_ms": float(kodim01["hkn_enc_ms"]),
    }


def main():
    probe_src_max = 2 * 1024 * 1024
    probe_ratio_min = 20
    probe_ratio_max_values = [80, 120]
    min_gain_values = [256, 512, 1024]
    opt_max_matches_values = [1, 2, 4]
    opt_lit_max_values = [32, 64, 128]

    baseline_csv = OUT_DIR / "phase9w_optparse_gate_sweep_baseline_strategy1_runs1.csv"
    subprocess.run(
        [
            "taskset",
            "-c",
            "0",
            str(BENCH),
            "--runs",
            "1",
            "--warmup",
            "0",
            "--preset",
            "max",
            "--out",
            str(baseline_csv),
        ],
        cwd=REPO,
        env={
            **os.environ,
            "HAKONYANS_THREADS": "1",
            "HKN_MAX_LZ_MATCH_STRATEGY": "1",
            "OMP_NUM_THREADS": "1",
        },
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    baseline = parse_metrics(baseline_csv)

    out_summary = OUT_DIR / "phase9w_optparse_gate_sweep_runs1_20260214.csv"
    rows = []
    idx = 0
    total = (
        len(probe_ratio_max_values)
        * len(min_gain_values)
        * len(opt_max_matches_values)
        * len(opt_lit_max_values)
    )
    for ratio_max in probe_ratio_max_values:
        for min_gain in min_gain_values:
            for max_matches in opt_max_matches_values:
                for lit_max in opt_lit_max_values:
                    idx += 1
                    out_csv = OUT_DIR / (
                        f"phase9w_optparse_gate_r1_{idx:03d}_"
                        f"rmx{ratio_max}_g{min_gain}_m{max_matches}_l{lit_max}.csv"
                    )
                    env_extra = {
                        "HKN_LZ_OPTPARSE_PROBE_SRC_MAX": probe_src_max,
                        "HKN_LZ_OPTPARSE_PROBE_RATIO_MIN": probe_ratio_min,
                        "HKN_LZ_OPTPARSE_PROBE_RATIO_MAX": ratio_max,
                        "HKN_LZ_OPTPARSE_MIN_GAIN_BYTES": min_gain,
                        "HKN_LZ_OPTPARSE_MAX_MATCHES": max_matches,
                        "HKN_LZ_OPTPARSE_LIT_MAX": lit_max,
                    }
                    run_bench(env_extra, out_csv, runs=1, warmup=0)
                    m = parse_metrics(out_csv)
                    d_bytes = m["total_hkn_bytes"] - baseline["total_hkn_bytes"]
                    d_enc = m["median_enc_ms"] - baseline["median_enc_ms"]
                    score = 0.0
                    if d_bytes < 0:
                        score = (-d_bytes) / max(1.0, d_enc if d_enc > 0 else 1.0)
                    rows.append(
                        {
                            "combo_idx": idx,
                            "ratio_min": probe_ratio_min,
                            "ratio_max": ratio_max,
                            "min_gain_bytes": min_gain,
                            "opt_max_matches": max_matches,
                            "opt_lit_max": lit_max,
                            "total_hkn_bytes": m["total_hkn_bytes"],
                            "d_total_hkn_bytes_vs_s1": d_bytes,
                            "median_enc_ms": f"{m['median_enc_ms']:.6f}",
                            "d_median_enc_ms_vs_s1": f"{d_enc:.6f}",
                            "median_dec_ms": f"{m['median_dec_ms']:.6f}",
                            "median_png_over_hkn": f"{m['median_png_over_hkn']:.6f}",
                            "kodim01_hkn_bytes": m["kodim01_hkn_bytes"],
                            "kodim01_enc_ms": f"{m['kodim01_enc_ms']:.6f}",
                            "score_gain_per_ms": f"{score:.6f}",
                            "csv_path": out_csv.name,
                        }
                    )
                    print(f"[{idx:03d}/{total}] done: rmax={ratio_max} gain={min_gain} m={max_matches} l={lit_max}")

    rows.sort(
        key=lambda r: (
            int(r["d_total_hkn_bytes_vs_s1"]) > 0,
            int(r["d_total_hkn_bytes_vs_s1"]),
            float(r["d_median_enc_ms_vs_s1"]),
        )
    )
    with open(out_summary, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    top_out = OUT_DIR / "phase9w_optparse_gate_sweep_top10_20260214.txt"
    with open(top_out, "w") as f:
        f.write("Baseline(strategy=1):\n")
        f.write(str(baseline) + "\n\n")
        f.write("Top 10 by (size gain then enc delta):\n")
        for r in rows[:10]:
            f.write(str(r) + "\n")

    print(f"Summary: {out_summary}")
    print(f"Top10:   {top_out}")


if __name__ == "__main__":
    main()
