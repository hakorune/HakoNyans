#!/usr/bin/env python3
import argparse
import csv
import math
import os
import statistics
import subprocess
import time
from pathlib import Path


DEFAULT_IMAGES = [
    ("UI", "test_images/ui/browser.ppm"),
    ("UI", "test_images/ui/vscode.ppm"),
    ("UI", "test_images/ui/terminal.ppm"),
    ("Anime", "test_images/anime/anime_girl_portrait.ppm"),
    ("Anime", "test_images/anime/anime_sunset.ppm"),
    ("Photo", "test_images/photo/nature_01.ppm"),
    ("Photo", "test_images/photo/nature_02.ppm"),
]


def run_cmd(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(
            f"Command failed ({p.returncode}): {' '.join(cmd)}\nSTDOUT:\n{p.stdout}\nSTDERR:\n{p.stderr}"
        )
    return p.stdout, p.stderr


def run_timed(cmd):
    start = time.perf_counter()
    run_cmd(cmd)
    end = time.perf_counter()
    return (end - start) * 1000.0


def load_ppm(path: Path):
    with path.open("rb") as f:
        if f.readline().strip() != b"P6":
            raise RuntimeError(f"Not P6 PPM: {path}")
        tokens = []
        while len(tokens) < 3:
            line = f.readline()
            if not line:
                raise RuntimeError(f"Unexpected EOF in header: {path}")
            line = line.strip()
            if not line or line.startswith(b"#"):
                continue
            tokens.extend(line.split())
        w, h, maxv = map(int, tokens[:3])
        if maxv != 255:
            raise RuntimeError(f"Unsupported maxv {maxv}: {path}")
        data = f.read(w * h * 3)
        if len(data) != w * h * 3:
            raise RuntimeError(f"PPM truncated: {path}")
    return w, h, data


def psnr(a: bytes, b: bytes):
    if len(a) != len(b):
        n = min(len(a), len(b))
        a = a[:n]
        b = b[:n]
    mse = sum((x - y) * (x - y) for x, y in zip(a, b)) / len(a)
    if mse < 1e-12:
        return 99.0
    return 10.0 * math.log10((255.0 * 255.0) / mse)


def add_summary(rows):
    by_codec_cat = {}
    for r in rows:
        key = (r["codec"], r["category"])
        by_codec_cat.setdefault(key, []).append(r)

    summary = []
    for (codec, category), rs in sorted(by_codec_cat.items()):
        summary.append(
            {
                "codec": codec,
                "category": category,
                "count": len(rs),
                "avg_size_kb": round(sum(x["size_kb"] for x in rs) / len(rs), 3),
                "avg_psnr_db": round(sum(x["psnr_db"] for x in rs) / len(rs), 4),
                "avg_encode_ms": round(sum(x["encode_ms"] for x in rs) / len(rs), 4),
                "avg_decode_ms": round(sum(x["decode_ms"] for x in rs) / len(rs), 4),
            }
        )
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="./build/hakonyans")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--work-dir", default="paper/results/codec_compare_artifacts")
    ap.add_argument("--out-detail", default="paper/results/lossy_jpeg_hkn_detail.csv")
    ap.add_argument("--out-summary", default="paper/results/lossy_jpeg_hkn_summary.csv")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[2]
    hkn = (repo / args.bin).resolve()
    if not hkn.exists():
        raise FileNotFoundError(f"hakonyans not found: {hkn}")

    work = (repo / args.work_dir).resolve()
    work.mkdir(parents=True, exist_ok=True)

    rows = []
    for category, rel in DEFAULT_IMAGES:
        src = (repo / rel).resolve()
        if not src.exists():
            print(f"[warn] skip missing image: {src}")
            continue
        _, _, src_bytes = load_ppm(src)
        stem = src.stem

        # PNG lossless baseline
        png = work / f"{stem}.png"
        png_dec = work / f"{stem}_png_dec.ppm"
        png_enc = []
        png_dec_t = []
        # direct shell redirection timing above is not valid through subprocess list,
        # so use explicit shell command for PNG only.
        for _ in range(args.runs):
            t0 = time.perf_counter()
            subprocess.run(f"pnmtopng '{src}' > '{png}'", shell=True, check=True)
            png_enc.append((time.perf_counter() - t0) * 1000.0)
            t1 = time.perf_counter()
            subprocess.run(f"pngtopnm '{png}' > '{png_dec}'", shell=True, check=True)
            png_dec_t.append((time.perf_counter() - t1) * 1000.0)
        _, _, png_dec_bytes = load_ppm(png_dec)
        rows.append(
            {
                "category": category,
                "image": rel,
                "codec": "PNG_lossless",
                "quality": 100,
                "size_bytes": png.stat().st_size,
                "size_kb": round(png.stat().st_size / 1024.0, 3),
                "psnr_db": 99.0 if src_bytes == png_dec_bytes else round(psnr(src_bytes, png_dec_bytes), 4),
                "encode_ms": round(statistics.median(png_enc), 4),
                "decode_ms": round(statistics.median(png_dec_t), 4),
            }
        )

        # JPEG q75/q90
        for q in (75, 90):
            jpg = work / f"{stem}_q{q}.jpg"
            dec = work / f"{stem}_q{q}_jpg_dec.ppm"
            enc_t = []
            dec_t = []
            for _ in range(args.runs):
                enc_t.append(run_timed(["cjpeg", "-quality", str(q), "-optimize", "-outfile", str(jpg), str(src)]))
                dec_t.append(run_timed(["djpeg", "-outfile", str(dec), str(jpg)]))
            _, _, dec_bytes = load_ppm(dec)
            rows.append(
                {
                    "category": category,
                    "image": rel,
                    "codec": f"JPEG_q{q}",
                    "quality": q,
                    "size_bytes": jpg.stat().st_size,
                    "size_kb": round(jpg.stat().st_size / 1024.0, 3),
                    "psnr_db": round(psnr(src_bytes, dec_bytes), 4),
                    "encode_ms": round(statistics.median(enc_t), 4),
                    "decode_ms": round(statistics.median(dec_t), 4),
                }
            )

        # HKN q75/q90, 4:4:4 + CfL=1
        for q in (75, 90):
            hkn_out = work / f"{stem}_hkn_q{q}.hkn"
            dec = work / f"{stem}_hkn_q{q}_dec.ppm"
            enc_t = []
            dec_t = []
            for _ in range(args.runs):
                enc_t.append(
                    run_timed(
                        [
                            str(hkn),
                            "encode",
                            str(src),
                            str(hkn_out),
                            str(q),
                            "0",  # 444
                            "1",  # CfL on
                            "0",  # screen profile off
                        ]
                    )
                )
                dec_t.append(run_timed([str(hkn), "decode", str(hkn_out), str(dec)]))
            _, _, dec_bytes = load_ppm(dec)
            rows.append(
                {
                    "category": category,
                    "image": rel,
                    "codec": f"HKN_q{q}_444_cfl1",
                    "quality": q,
                    "size_bytes": hkn_out.stat().st_size,
                    "size_kb": round(hkn_out.stat().st_size / 1024.0, 3),
                    "psnr_db": round(psnr(src_bytes, dec_bytes), 4),
                    "encode_ms": round(statistics.median(enc_t), 4),
                    "decode_ms": round(statistics.median(dec_t), 4),
                }
            )

        print(f"[ok] {category}/{src.name}")

    out_detail = (repo / args.out_detail).resolve()
    out_detail.parent.mkdir(parents=True, exist_ok=True)
    with out_detail.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "category",
                "image",
                "codec",
                "quality",
                "size_bytes",
                "size_kb",
                "psnr_db",
                "encode_ms",
                "decode_ms",
            ],
        )
        w.writeheader()
        w.writerows(rows)

    summary = add_summary(rows)
    out_summary = (repo / args.out_summary).resolve()
    with out_summary.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "codec",
                "category",
                "count",
                "avg_size_kb",
                "avg_psnr_db",
                "avg_encode_ms",
                "avg_decode_ms",
            ],
        )
        w.writeheader()
        w.writerows(summary)

    print(f"Wrote detail: {out_detail}")
    print(f"Wrote summary: {out_summary}")


if __name__ == "__main__":
    main()
