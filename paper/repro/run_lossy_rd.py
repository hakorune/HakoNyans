#!/usr/bin/env python3
import argparse
import csv
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path


DEFAULT_IMAGES = [
    ("ui", "test_images/ui/browser.ppm"),
    ("ui", "test_images/ui/vscode.ppm"),
    ("ui", "test_images/ui/terminal.ppm"),
    ("anime", "test_images/anime/anime_girl_portrait.ppm"),
    ("anime", "test_images/anime/anime_sunset.ppm"),
    ("game", "test_images/game/minecraft_2d.ppm"),
    ("game", "test_images/game/retro.ppm"),
    ("photo", "test_images/photo/nature_01.ppm"),
    ("photo", "test_images/photo/nature_02.ppm"),
    ("natural", "test_images/kodak/kodim03.ppm"),
    ("natural", "test_images/kodak/hd_01.ppm"),
]

ENC_MS_RE = re.compile(r"Encoded in\s+([0-9.]+)\s+ms")
DEC_MS_RE = re.compile(r"Decoded in\s+([0-9.]+)\s+ms")


def run_cmd(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(
            f"Command failed ({p.returncode}): {' '.join(cmd)}\nSTDOUT:\n{p.stdout}\nSTDERR:\n{p.stderr}"
        )
    return p.stdout + p.stderr


def parse_ms(text, regex, kind):
    m = regex.search(text)
    if not m:
        raise RuntimeError(f"Failed to parse {kind} time from output:\n{text}")
    return float(m.group(1))


def load_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise RuntimeError(f"Not a P6 PPM: {path}")

        def next_token():
            while True:
                line = f.readline()
                if not line:
                    raise RuntimeError(f"Unexpected EOF in PPM header: {path}")
                line = line.strip()
                if not line or line.startswith(b"#"):
                    continue
                for tok in line.split():
                    if tok.startswith(b"#"):
                        break
                    yield tok

        toks = next_token()
        w = int(next(toks))
        h = int(next(toks))
        maxv = int(next(toks))
        if maxv != 255:
            raise RuntimeError(f"Unsupported PPM maxval={maxv}: {path}")
        data = f.read(w * h * 3)
        if len(data) != w * h * 3:
            raise RuntimeError(f"PPM pixel data truncated: {path}")
        return w, h, data


def calc_psnr_bytes(a, b):
    if len(a) != len(b):
        n = min(len(a), len(b))
        a = a[:n]
        b = b[:n]
    if not a:
        return 0.0
    sse = 0
    for x, y in zip(a, b):
        d = x - y
        sse += d * d
    mse = sse / len(a)
    if mse < 1e-12:
        return 100.0
    return 10.0 * __import__("math").log10((255.0 * 255.0) / mse)


def main():
    ap = argparse.ArgumentParser(description="Run lossy RD/Speed measurements for paper")
    ap.add_argument("--bin", default="./build/hakonyans", help="Path to hakonyans CLI binary")
    ap.add_argument("--qualities", default="30,50,70,90", help="Comma-separated quality list")
    ap.add_argument("--runs", type=int, default=3, help="Encode/decode runs per point")
    ap.add_argument("--out-csv", default="paper/results/lossy_rd.csv", help="CSV output path")
    ap.add_argument(
        "--artifacts-dir",
        default="paper/results/artifacts",
        help="Directory to store temporary encoded/decoded files",
    )
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    hkn_bin = (repo_root / args.bin).resolve()
    out_csv = (repo_root / args.out_csv).resolve()
    artifacts_dir = (repo_root / args.artifacts_dir).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    out_csv.parent.mkdir(parents=True, exist_ok=True)

    if not hkn_bin.exists():
        raise FileNotFoundError(f"hakonyans binary not found: {hkn_bin}")

    qualities = [int(q.strip()) for q in args.qualities.split(",") if q.strip()]
    rows = []

    for category, rel_path in DEFAULT_IMAGES:
        ppm_path = (repo_root / rel_path).resolve()
        if not ppm_path.exists():
            print(f"[warn] skip missing image: {ppm_path}")
            continue

        width, height, src_bytes_data = load_ppm(ppm_path)
        raw_bytes = width * height * 3
        src_file_bytes = os.path.getsize(ppm_path)

        for q in qualities:
            enc_ms_samples = []
            dec_ms_samples = []
            hkn_bytes = None
            psnr_db = None

            stem = ppm_path.stem.replace(" ", "_")
            hkn_file = artifacts_dir / f"{category}_{stem}_q{q}.hkn"
            dec_file = artifacts_dir / f"{category}_{stem}_q{q}.ppm"

            for run_idx in range(args.runs):
                out_enc = run_cmd(
                    [
                        str(hkn_bin),
                        "encode",
                        str(ppm_path),
                        str(hkn_file),
                        str(q),
                        "1",  # 4:2:0
                        "1",  # CfL on
                        "0",  # screen profile off
                    ]
                )
                enc_ms_samples.append(parse_ms(out_enc, ENC_MS_RE, "encode"))
                hkn_bytes = os.path.getsize(hkn_file)

                out_dec = run_cmd([str(hkn_bin), "decode", str(hkn_file), str(dec_file)])
                dec_ms_samples.append(parse_ms(out_dec, DEC_MS_RE, "decode"))

                if run_idx == 0:
                    _, _, dec_bytes_data = load_ppm(dec_file)
                    psnr_db = calc_psnr_bytes(src_bytes_data, dec_bytes_data)

            enc_ms = statistics.median(enc_ms_samples)
            dec_ms = statistics.median(dec_ms_samples)
            bpp = (8.0 * hkn_bytes) / (width * height)
            ratio_vs_src = src_file_bytes / hkn_bytes if hkn_bytes else 0.0
            ratio_vs_raw = hkn_bytes / raw_bytes if raw_bytes else 0.0

            rows.append(
                {
                    "category": category,
                    "image": rel_path,
                    "quality": q,
                    "width": width,
                    "height": height,
                    "raw_bytes": raw_bytes,
                    "src_ppm_bytes": src_file_bytes,
                    "hkn_bytes": hkn_bytes,
                    "bpp": round(bpp, 6),
                    "psnr_db": round(psnr_db, 4),
                    "encode_ms_median": round(enc_ms, 4),
                    "decode_ms_median": round(dec_ms, 4),
                    "ratio_src_over_hkn": round(ratio_vs_src, 6),
                    "ratio_hkn_over_raw": round(ratio_vs_raw, 6),
                    "runs": args.runs,
                }
            )
            print(
                f"[ok] {category}/{ppm_path.name} Q{q}: "
                f"hkn={hkn_bytes}B psnr={psnr_db:.2f} enc={enc_ms:.2f}ms dec={dec_ms:.2f}ms"
            )

    fieldnames = [
        "category",
        "image",
        "quality",
        "width",
        "height",
        "raw_bytes",
        "src_ppm_bytes",
        "hkn_bytes",
        "bpp",
        "psnr_db",
        "encode_ms_median",
        "decode_ms_median",
        "ratio_src_over_hkn",
        "ratio_hkn_over_raw",
        "runs",
    ]
    with open(out_csv, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)

    print(f"\nSaved: {out_csv}")
    print(f"Rows: {len(rows)}")


if __name__ == "__main__":
    main()
