#!/usr/bin/env python3
import argparse
import csv
import re
from pathlib import Path


ROW_RE_V1 = re.compile(
    r"^\s*(?P<image>\S+)\s+"
    r"(?P<png_kb>[0-9.]+)\s+"
    r"(?P<hkn_kb>[0-9.]+)\s+"
    r"(?P<size_pct>[+\-][0-9]+)%\s+"
    r"(?P<enc>[0-9.]+)x\s+"
    r"(?P<dec>[0-9.]+)x"
    r"(?P<category>[A-Za-z]+)\s*$"
)

ROW_RE_V2 = re.compile(
    r"^\s*(?P<image>\S+)\s+"
    r"(?P<png_kb>[0-9.]+)\s+"
    r"(?P<hkn_kb>[0-9.]+)\s+"
    r"(?P<size_pct>[+\-][0-9]+)%\s+"
    r"(?P<png_dec_ms>[0-9.]+)ms\s+"
    r"(?P<hkn_dec_ms>[0-9.]+)ms"
    r"(?P<category>[A-Za-z]+)\s*$"
)


def parse_rows(text: str):
    rows = []
    for line in text.splitlines():
        d = None
        m2 = ROW_RE_V2.match(line)
        if m2:
            d = m2.groupdict()
        else:
            m1 = ROW_RE_V1.match(line)
            if m1:
                d = m1.groupdict()
        if d is None:
            continue

        png_kb = float(d["png_kb"])
        hkn_kb = float(d["hkn_kb"])
        png_dec_ms = float(d["png_dec_ms"]) if d.get("png_dec_ms") is not None else 0.0
        hkn_dec_ms = float(d["hkn_dec_ms"]) if d.get("hkn_dec_ms") is not None else 0.0
        dec_speedup = (
            (png_dec_ms / hkn_dec_ms) if (png_dec_ms > 0 and hkn_dec_ms > 0)
            else float(d["dec"])
        )

        rows.append(
            {
                "image": d["image"],
                "category": d["category"],
                "png_kb": png_kb,
                "hkn_kb": hkn_kb,
                "png_dec_ms": round(png_dec_ms, 4),
                "hkn_dec_ms": round(hkn_dec_ms, 4),
                "size_ratio_hkn_over_png": round(hkn_kb / png_kb, 4) if png_kb > 0 else 0.0,
                "size_change_percent": int(d["size_pct"]),
                "enc_speedup_hkn_vs_png": float(d["enc"]) if d.get("enc") is not None else 0.0,
                "dec_speedup_hkn_vs_png": round(dec_speedup, 4),
            }
        )
    return rows


def category_summary(rows):
    by_cat = {}
    for r in rows:
        cat = r["category"]
        by_cat.setdefault(cat, []).append(r)

    out = []
    for cat, rs in sorted(by_cat.items()):
        n = len(rs)
        out.append(
            {
                "category": cat,
                "count": n,
                "avg_size_ratio_hkn_over_png": round(
                    sum(r["size_ratio_hkn_over_png"] for r in rs) / n, 4
                ),
                "avg_enc_speedup_hkn_vs_png": round(
                    sum(r["enc_speedup_hkn_vs_png"] for r in rs) / n, 4
                ),
                "avg_dec_speedup_hkn_vs_png": round(
                    sum(r["dec_speedup_hkn_vs_png"] for r in rs) / n, 4
                ),
                "avg_png_dec_ms": round(
                    sum(r["png_dec_ms"] for r in rs) / n, 4
                ),
                "avg_hkn_dec_ms": round(
                    sum(r["hkn_dec_ms"] for r in rs) / n, 4
                ),
            }
        )
    return out


def write_csv(path: Path, rows, fieldnames):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--input",
        default="paper/results/lossless_png_compare_latest.txt",
        help="bench_png_compare output text path",
    )
    ap.add_argument(
        "--out-detail",
        default="paper/results/lossless_png_compare_detail.csv",
    )
    ap.add_argument(
        "--out-summary",
        default="paper/results/lossless_png_compare_summary.csv",
    )
    ap.add_argument(
        "--focus-categories",
        default="UI,Anime,Photo",
        help="comma separated categories to keep for paper table",
    )
    ap.add_argument(
        "--out-focus",
        default="paper/results/lossless_png_compare_focus.csv",
    )
    args = ap.parse_args()

    in_path = Path(args.input)
    text = in_path.read_text(encoding="utf-8")
    rows = parse_rows(text)
    if not rows:
        raise RuntimeError(f"No benchmark rows parsed from: {in_path}")

    write_csv(
        Path(args.out_detail),
        rows,
        [
            "image",
            "category",
            "png_kb",
            "hkn_kb",
            "png_dec_ms",
            "hkn_dec_ms",
            "size_ratio_hkn_over_png",
            "size_change_percent",
            "enc_speedup_hkn_vs_png",
            "dec_speedup_hkn_vs_png",
        ],
    )
    write_csv(
        Path(args.out_summary),
        category_summary(rows),
        [
            "category",
            "count",
            "avg_size_ratio_hkn_over_png",
            "avg_enc_speedup_hkn_vs_png",
            "avg_dec_speedup_hkn_vs_png",
            "avg_png_dec_ms",
            "avg_hkn_dec_ms",
        ],
    )

    focus = {x.strip() for x in args.focus_categories.split(",") if x.strip()}
    focus_rows = [r for r in rows if r["category"] in focus]
    write_csv(
        Path(args.out_focus),
        focus_rows,
        [
            "image",
            "category",
            "png_kb",
            "hkn_kb",
            "png_dec_ms",
            "hkn_dec_ms",
            "size_ratio_hkn_over_png",
            "size_change_percent",
            "enc_speedup_hkn_vs_png",
            "dec_speedup_hkn_vs_png",
        ],
    )

    print(f"Parsed rows: {len(rows)}")
    print(f"Focus rows: {len(focus_rows)}")
    print(f"Wrote: {args.out_detail}")
    print(f"Wrote: {args.out_summary}")
    print(f"Wrote: {args.out_focus}")


if __name__ == "__main__":
    main()
