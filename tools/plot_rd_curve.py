#!/usr/bin/env python3
import matplotlib.pyplot as plt
import sys
import os
import csv

# Expected CSV format: Codec,Quality,Size(KB),PSNR(dB),DecTime(ms),BPP
# We will parse the output of bench_compare.sh manually or expect a clean CSV

def parse_results(filepath):
    data = {}
    
    if not os.path.exists(filepath):
        print(f"File not found: {filepath}")
        return data

    with open(filepath, 'r') as f:
        # Skip header/lines until we find data
        # Data lines format from script:
        # Image Codec Quality Size(KB) PSNR(dB) DecTime(ms)
        
        for line in f:
            if line.startswith('---') or line.startswith('Image') or line.startswith('==='):
                continue
            
            parts = line.split()
            if len(parts) < 6:
                continue
                
            # parts: [0]Image [1]Codec [2]Quality [3]Size [4]PSNR [5]Time
            # We need to calculate BPP. Assuming Image size is fixed or known?
            # Or we can just read Size and PSNR.
            # BPP calculation requires image dimensions.
            # Let's approximate or just plot Size vs PSNR if dimensions are unknown.
            # But wait, instruction says "PSNR vs bpp".
            # We can infer dimensions from standard Kodak images (768x512) or test images.
            # Let's hardcode 768x512 for Kodak or use a default.
            
            codec = parts[1]
            try:
                size_kb = float(parts[3])
                psnr_str = parts[4]
                if psnr_str == "N/A": continue
                psnr = float(psnr_str)
                
                # Assume 768x512 for now if using Kodak, or 1920x1080 for HD
                # Let's calculate BPP based on file size (Size is in KB)
                # BPP = (Size_KB * 1024 * 8) / (Pixels)
                # We'll assume Kodak size for now: 768 * 512 = 393216
                pixels = 768 * 512
                if "1920x1080" in line: pixels = 1920 * 1080
                
                bpp = (size_kb * 1024 * 8) / pixels
                
                if codec not in data:
                    data[codec] = {'bpp': [], 'psnr': []}
                
                data[codec]['bpp'].append(bpp)
                data[codec]['psnr'].append(psnr)
            except ValueError:
                continue
                
    return data

def main():
    if len(sys.argv) < 2:
        print("Usage: plot_rd_curve.py <results.txt>")
        return

    data = parse_results(sys.argv[1])
    
    plt.figure(figsize=(10, 6))
    markers = {'HakoNyans': 'o', 'JPEG': 's', 'JPEG-XL': '^', 'AVIF': 'D'}
    
    for codec, d in data.items():
        # Sort by BPP
        points = sorted(zip(d['bpp'], d['psnr']))
        bpps = [p[0] for p in points]
        psnrs = [p[1] for p in points]
        
        marker = markers.get(codec, 'x')
        plt.plot(bpps, psnrs, marker=marker, label=codec, linewidth=2)

    plt.xlabel('bits per pixel (bpp)')
    plt.ylabel('PSNR (dB)')
    plt.title('Rate-Distortion Curve')
    plt.legend()
    plt.grid(True)
    
    out_path = 'bench_results/rd_curve.png'
    os.makedirs('bench_results', exist_ok=True)
    plt.savefig(out_path, dpi=150)
    print(f"Saved: {out_path}")

if __name__ == '__main__':
    main()
