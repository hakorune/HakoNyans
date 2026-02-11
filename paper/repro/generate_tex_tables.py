#!/usr/bin/env python3
import csv
from pathlib import Path


def read_csv(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def esc(s: str):
    return s.replace("_", r"\_")


def write_lossless_detail(rows, out_path: Path):
    out_path.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    lines.append(r"\begin{tabular}{llrrrrr}")
    lines.append(r"\toprule")
    lines.append(r"Image & Category & PNG (KB) & HKN (KB) & HKN/PNG & PNG Dec (ms) & HKN Dec (ms) \\")
    lines.append(r"\midrule")
    for r in rows:
        lines.append(
            f"{esc(r['image'])} & {esc(r['category'])} & "
            f"{float(r['png_kb']):.1f} & {float(r['hkn_kb']):.1f} & "
            f"{float(r['size_ratio_hkn_over_png']):.2f}x & "
            f"{float(r['png_dec_ms']):.2f} & {float(r['hkn_dec_ms']):.2f} \\\\"
        )
    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_lossy_summary(rows, out_path: Path, codecs):
    out_path.parent.mkdir(parents=True, exist_ok=True)
    wanted = [r for r in rows if r["codec"] in codecs]
    wanted.sort(key=lambda x: (x["category"], x["codec"]))

    lines = []
    lines.append(r"\begin{tabular}{llrrrr}")
    lines.append(r"\toprule")
    lines.append(r"Category & Codec & Avg Size (KB) & Avg PSNR (dB) & Enc (ms) & Dec (ms) \\")
    lines.append(r"\midrule")
    for r in wanted:
        lines.append(
            f"{esc(r['category'])} & {esc(r['codec'])} & "
            f"{float(r['avg_size_kb']):.1f} & {float(r['avg_psnr_db']):.2f} & "
            f"{float(r['avg_encode_ms']):.1f} & {float(r['avg_decode_ms']):.1f} \\\\"
        )
    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    repo = Path(__file__).resolve().parents[2]
    lossless_focus = repo / "paper/results/lossless_png_compare_focus.csv"
    lossy_summary = repo / "paper/results/lossy_jpeg_hkn_summary.csv"

    lossless_rows = read_csv(lossless_focus)
    lossy_rows = read_csv(lossy_summary)

    write_lossless_detail(
        lossless_rows,
        repo / "paper/tables/table_lossless_png_focus.tex",
    )

    write_lossy_summary(
        lossy_rows,
        repo / "paper/tables/table_lossy_q75_summary.tex",
        codecs=["PNG_lossless", "JPEG_q75", "HKN_q75_444_cfl1"],
    )

    write_lossy_summary(
        lossy_rows,
        repo / "paper/tables/table_lossy_q90_summary.tex",
        codecs=["PNG_lossless", "JPEG_q90", "HKN_q90_444_cfl1"],
    )

    print("Wrote:")
    print(" - paper/tables/table_lossless_png_focus.tex")
    print(" - paper/tables/table_lossy_q75_summary.tex")
    print(" - paper/tables/table_lossy_q90_summary.tex")


if __name__ == "__main__":
    main()
