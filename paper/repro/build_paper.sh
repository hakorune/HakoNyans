#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

cd bench
../bench_png_compare | tee ../paper/results/lossless_png_compare_latest.txt
cd ..

python3 paper/repro/parse_lossless_png_compare.py
python3 paper/repro/run_lossy_jpeg_hkn_compare.py --runs 2
python3 paper/repro/generate_qualitative_figures.py
python3 paper/repro/generate_tex_tables.py

cd paper
latexmk -lualatex -interaction=nonstopmode paper_ja.tex

echo "Built: $ROOT_DIR/paper/paper_ja.pdf"
