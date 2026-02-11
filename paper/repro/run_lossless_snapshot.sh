#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/paper/results"
mkdir -p "$OUT_DIR"

cd "$ROOT_DIR/build"

echo "[1/3] bench_png_compare ..."
./bench_png_compare > "$OUT_DIR/lossless_png_compare.txt"

echo "[2/3] bench_decode ..."
./bench_decode > "$OUT_DIR/lossless_decode_bench.txt"

echo "[3/3] bench_bit_accounting (representative images) ..."
./bench_bit_accounting ../test_images/ui/vscode.ppm --lossless > "$OUT_DIR/lossless_bit_accounting_vscode.txt"
./bench_bit_accounting ../test_images/photo/nature_01.ppm --lossless > "$OUT_DIR/lossless_bit_accounting_nature01.txt"
./bench_bit_accounting ../test_images/kodak/hd_01.ppm --lossless > "$OUT_DIR/lossless_bit_accounting_hd01.txt"

echo "Saved lossless snapshot under: $OUT_DIR"
