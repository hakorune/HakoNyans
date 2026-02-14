#!/usr/bin/env python3
import csv
import itertools
import os
import statistics
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
BENCH = REPO / "build" / "bench_png_compare"
OUT_DIR = REPO / "bench_results"
OUT_DIR.mkdir(parents=True, exist_ok=True)

IMAGES = ["kodim01", "kodim02", "kodim03", "hd_01", "nature_01", "nature_02"]


def median(vals):
    return statistics.median(vals) if vals else 0.0


def probe_enable_env_key_for_preset(preset):
    if preset == "fast":
        return "HKN_FAST_FILTER_LO_LZ_PROBE"
    if preset == "balanced":
        return "HKN_BALANCED_FILTER_LO_LZ_PROBE"
    return "HKN_MAX_FILTER_LO_LZ_PROBE"


def run_bench(out_csv, preset, runs, warmup, env_extra):
    env = os.environ.copy()
    env.update(
        {
            "HAKONYANS_THREADS": "1",
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
        preset,
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

    total_hkn_bytes = sum(int(row["hkn_bytes"]) for row in rows)
    med_png_over_hkn = median([float(row["png_over_hkn"]) for row in rows])
    med_enc_ms = median([float(row["hkn_enc_ms"]) for row in rows])
    med_dec_ms = median([float(row["hkn_dec_ms"]) for row in rows])
    probe_enabled = sum(int(row["hkn_enc_lo_lz_probe_enabled"]) for row in rows)
    probe_checked = sum(int(row["hkn_enc_lo_lz_probe_checked"]) for row in rows)
    probe_pass = sum(int(row["hkn_enc_lo_lz_probe_pass"]) for row in rows)
    probe_skip = sum(int(row["hkn_enc_lo_lz_probe_skip"]) for row in rows)
    probe_sample = sum(int(row["hkn_enc_lo_lz_probe_sample_bytes"]) for row in rows)
    probe_sample_lz = sum(int(row["hkn_enc_lo_lz_probe_sample_lz_bytes"]) for row in rows)
    probe_sample_wrapped = sum(
        int(row["hkn_enc_lo_lz_probe_sample_wrapped_bytes"]) for row in rows
    )

    return {
        "total_hkn_bytes": total_hkn_bytes,
        "median_png_over_hkn": med_png_over_hkn,
        "median_enc_ms": med_enc_ms,
        "median_dec_ms": med_dec_ms,
        "probe_enabled": probe_enabled,
        "probe_checked": probe_checked,
        "probe_pass": probe_pass,
        "probe_skip": probe_skip,
        "probe_sample_bytes": probe_sample,
        "probe_sample_lz_bytes": probe_sample_lz,
        "probe_sample_wrapped_bytes": probe_sample_wrapped,
    }


def main():
    preset = os.environ.get("HKN_SWEEP_PRESET", "max").strip().lower()
    if preset not in {"fast", "balanced", "max"}:
        raise RuntimeError(f"Unsupported preset: {preset}")

    runs = int(os.environ.get("HKN_SWEEP_RUNS", "1"))
    warmup = int(os.environ.get("HKN_SWEEP_WARMUP", "0"))

    threshold_permille_values = [960, 980, 1000, 1030, 1060, 1100]
    sample_bytes_values = [2048, 4096, 8192]
    min_raw_bytes_values = [2048, 4096]

    enable_key = probe_enable_env_key_for_preset(preset)
    baseline_env = {enable_key: "1"}
    baseline_csv = OUT_DIR / f"phase9w_filter_lo_probe_baseline_{preset}_runs{runs}.csv"
    run_bench(baseline_csv, preset, runs, warmup, baseline_env)
    baseline = parse_metrics(baseline_csv)

    combos = list(
        itertools.product(
            threshold_permille_values,
            sample_bytes_values,
            min_raw_bytes_values,
        )
    )
    rows = []
    for idx, (threshold_permille, sample_bytes, min_raw_bytes) in enumerate(combos, 1):
        out_csv = OUT_DIR / (
            f"phase9w_filter_lo_probe_{preset}_{idx:03d}_"
            f"th{threshold_permille}_s{sample_bytes}_mr{min_raw_bytes}.csv"
        )
        env_extra = {
            enable_key: "1",
            "HKN_FILTER_LO_LZ_PROBE_THRESHOLD_PERMILLE": threshold_permille,
            "HKN_FILTER_LO_LZ_PROBE_SAMPLE_BYTES": sample_bytes,
            "HKN_FILTER_LO_LZ_PROBE_MIN_RAW_BYTES": min_raw_bytes,
        }
        run_bench(out_csv, preset, runs, warmup, env_extra)
        m = parse_metrics(out_csv)

        d_total = m["total_hkn_bytes"] - baseline["total_hkn_bytes"]
        d_enc = m["median_enc_ms"] - baseline["median_enc_ms"]
        d_dec = m["median_dec_ms"] - baseline["median_dec_ms"]
        d_ratio = m["median_png_over_hkn"] - baseline["median_png_over_hkn"]

        gate = (
            m["total_hkn_bytes"] <= baseline["total_hkn_bytes"]
            and m["median_png_over_hkn"] >= baseline["median_png_over_hkn"]
        )
        skip_rate = 0.0
        if m["probe_checked"] > 0:
            skip_rate = 100.0 * float(m["probe_skip"]) / float(m["probe_checked"])

        rows.append(
            {
                "combo_idx": idx,
                "preset": preset,
                "threshold_permille": threshold_permille,
                "sample_bytes": sample_bytes,
                "min_raw_bytes": min_raw_bytes,
                "total_hkn_bytes": m["total_hkn_bytes"],
                "d_total_hkn_bytes_vs_base": d_total,
                "median_png_over_hkn": f"{m['median_png_over_hkn']:.6f}",
                "d_median_png_over_hkn_vs_base": f"{d_ratio:.6f}",
                "median_enc_ms": f"{m['median_enc_ms']:.6f}",
                "d_median_enc_ms_vs_base": f"{d_enc:.6f}",
                "median_dec_ms": f"{m['median_dec_ms']:.6f}",
                "d_median_dec_ms_vs_base": f"{d_dec:.6f}",
                "probe_enabled_total": m["probe_enabled"],
                "probe_checked_total": m["probe_checked"],
                "probe_pass_total": m["probe_pass"],
                "probe_skip_total": m["probe_skip"],
                "probe_skip_rate_pct": f"{skip_rate:.3f}",
                "probe_sample_bytes_total": m["probe_sample_bytes"],
                "probe_sample_lz_bytes_total": m["probe_sample_lz_bytes"],
                "probe_sample_wrapped_bytes_total": m["probe_sample_wrapped_bytes"],
                "gate": "PASS" if gate else "FAIL",
                "csv_path": out_csv.name,
            }
        )
        print(
            f"[{idx:02d}/{len(combos)}] th={threshold_permille} s={sample_bytes} "
            f"mr={min_raw_bytes} gate={'PASS' if gate else 'FAIL'}"
        )

    rows.sort(
        key=lambda r: (
            r["gate"] != "PASS",
            int(r["d_total_hkn_bytes_vs_base"]),
            float(r["d_median_enc_ms_vs_base"]),
        )
    )

    summary_csv = OUT_DIR / f"phase9w_filter_lo_probe_sweep_{preset}_runs{runs}.csv"
    with open(summary_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    top_txt = OUT_DIR / f"phase9w_filter_lo_probe_sweep_top10_{preset}_runs{runs}.txt"
    with open(top_txt, "w") as f:
        f.write("Baseline:\n")
        f.write(str(baseline) + "\n\n")
        f.write("Top 10 (gate, total_bytes, enc_ms):\n")
        for row in rows[:10]:
            f.write(str(row) + "\n")

    print(f"Baseline: {baseline_csv}")
    print(f"Summary:  {summary_csv}")
    print(f"Top10:    {top_txt}")


if __name__ == "__main__":
    main()
