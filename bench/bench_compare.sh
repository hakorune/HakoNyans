#!/bin/bash
# HakoNyans Improved Comparison Benchmark (HD focus)

set -e

INPUT_DIR="test_images/kodak"
OUTPUT_DIR="bench_results"
HAKONYANS="./build/hakonyans"
ITERATIONS=5 # HD is slower, so fewer iterations

PYTHON="./venv/bin/python3"
CALC_PSNR="./tools/calc_psnr.py"

mkdir -p $OUTPUT_DIR/{hakonyans,jpeg,jxl,avif}

# Detect tools
HAS_JPEG=1
HAS_JXL=1
HAS_AVIF=1

# Use our HD image
IMAGES="hd_01.ppm"

printf "%-15s %-10s %-12s %-12s %-10s %-10s\n" "Image" "Codec" "Quality" "Size(KB)" "PSNR(dB)" "DecTime(ms)"
echo "----------------------------------------------------------------"

for img_name in $IMAGES; do
    img_path="$INPUT_DIR/$img_name"
    base=$(basename $img_path .ppm)
    
    # === HakoNyans ===
    for Q in 50 75 90; do
        $HAKONYANS encode "$img_path" "$OUTPUT_DIR/hakonyans/${base}_q${Q}.hkn" $Q > /dev/null
        total_ms=0
        for i in $(seq 1 $ITERATIONS); do
            out=$($HAKONYANS decode "$OUTPUT_DIR/hakonyans/${base}_q${Q}.hkn" "$OUTPUT_DIR/hakonyans/${base}_q${Q}.ppm")
            ms=$(echo "$out" | grep "Decoded in" | awk '{print $3}')
            total_ms=$(echo "$total_ms + $ms" | bc)
        done
        avg_ms=$(echo "scale=2; $total_ms / $ITERATIONS" | bc)
        size_kb=$(du -k "$OUTPUT_DIR/hakonyans/${base}_q${Q}.hkn" | cut -f1)
        psnr=$($PYTHON $CALC_PSNR "$img_path" "$OUTPUT_DIR/hakonyans/${base}_q${Q}.ppm")
        printf "%-15s %-10s %-12s %-12s %-10s %-10s\n" "$base" "HakoNyans" "Q=$Q" "$size_kb" "$psnr" "$avg_ms"
    done
    
    # === JPEG (libjpeg-turbo) ===
    for Q in 50 75 90; do
        cjpeg -quality $Q -outfile "$OUTPUT_DIR/jpeg/${base}_q${Q}.jpg" "$img_path" > /dev/null 2>&1
        total_ms=0
        for i in $(seq 1 $ITERATIONS); do
            start=$(date +%s%N)
            djpeg -ppm -outfile "$OUTPUT_DIR/jpeg/${base}_q${Q}.ppm" "$OUTPUT_DIR/jpeg/${base}_q${Q}.jpg" > /dev/null 2>&1
            end=$(date +%s%N)
            elapsed=$(echo "scale=2; ($end - $start) / 1000000" | bc)
            total_ms=$(echo "$total_ms + $elapsed" | bc)
        done
        avg_ms=$(echo "scale=2; $total_ms / $ITERATIONS" | bc)
        size_kb=$(du -k "$OUTPUT_DIR/jpeg/${base}_q${Q}.jpg" | cut -f1)
        psnr=$($PYTHON $CALC_PSNR "$img_path" "$OUTPUT_DIR/jpeg/${base}_q${Q}.ppm")
        printf "%-15s %-10s %-12s %-12s %-10s %-10s\n" "$base" "JPEG" "Q=$Q" "$size_kb" "$psnr" "$avg_ms"
    done

    # === JPEG-XL ===
    for D in 1.5 1.0 0.5; do
         cjxl "$img_path" "$OUTPUT_DIR/jxl/${base}_d${D}.jxl" -d $D --effort 3 > /dev/null 2>&1
         total_ms=0
         for i in $(seq 1 $ITERATIONS); do
             start=$(date +%s%N)
             djxl "$OUTPUT_DIR/jxl/${base}_d${D}.jxl" "$OUTPUT_DIR/jxl/${base}_d${D}.ppm" > /dev/null 2>&1
             end=$(date +%s%N)
             elapsed=$(echo "scale=2; ($end - $start) / 1000000" | bc)
             total_ms=$(echo "$total_ms + $elapsed" | bc)
         done
         avg_ms=$(echo "scale=2; $total_ms / $ITERATIONS" | bc)
         size_kb=$(du -k "$OUTPUT_DIR/jxl/${base}_d${D}.jxl" | cut -f1)
         psnr=$($PYTHON $CALC_PSNR "$img_path" "$OUTPUT_DIR/jxl/${base}_d${D}.ppm")
         printf "%-15s %-10s %-12s %-12s %-10s %-10s\n" "$base" "JPEG-XL" "D=$D" "$size_kb" "$psnr" "$avg_ms"
    done
    
    # === AVIF ===
    for Q in 50 75; do # Fewer points for AVIF as it's slow
        avifenc -q $Q -s 8 "$img_path" "$OUTPUT_DIR/avif/${base}_q${Q}.avif" > /dev/null 2>&1
        total_ms=0
        for i in $(seq 1 $ITERATIONS); do
            start=$(date +%s%N)
            avifdec "$OUTPUT_DIR/avif/${base}_q${Q}.avif" "$OUTPUT_DIR/avif/${base}_q${Q}.ppm" > /dev/null 2>&1
            end=$(date +%s%N)
            elapsed=$(echo "scale=2; ($end - $start) / 1000000" | bc)
            total_ms=$(echo "$total_ms + $elapsed" | bc)
        done
        avg_ms=$(echo "scale=2; $total_ms / $ITERATIONS" | bc)
        size_kb=$(du -k "$OUTPUT_DIR/avif/${base}_q${Q}.avif" | cut -f1)
        psnr=$($PYTHON $CALC_PSNR "$img_path" "$OUTPUT_DIR/avif/${base}_q${Q}.ppm")
        printf "%-15s %-10s %-12s %-12s %-10s %-10s\n" "$base" "AVIF" "Q=$Q" "$size_kb" "$psnr" "$avg_ms"
    done
done
