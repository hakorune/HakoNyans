#!/usr/bin/env python3
import sys
import numpy as np
from PIL import Image

def load_ppm(path):
    try:
        img = Image.open(path).convert('RGB')
        return np.array(img, dtype=np.float64)
    except Exception as e:
        print(f"Error loading {path}: {e}", file=sys.stderr)
        sys.exit(1)

def calc_psnr(img1, img2):
    if img1.shape != img2.shape:
        print(f"Shape mismatch: {img1.shape} vs {img2.shape}", file=sys.stderr)
        # Resize img2 to match img1 (simple crop or pad, but for bench assume same size or crop to min)
        min_h = min(img1.shape[0], img2.shape[0])
        min_w = min(img1.shape[1], img2.shape[1])
        img1 = img1[:min_h, :min_w, :]
        img2 = img2[:min_h, :min_w, :]
        
    mse = np.mean((img1 - img2) ** 2)
    if mse < 1e-10:
        return 100.0
    return 10 * np.log10(255**2 / mse)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: calc_psnr.py <original.ppm> <compressed.ppm>")
        sys.exit(1)
    
    img1 = load_ppm(sys.argv[1])
    img2 = load_ppm(sys.argv[2])
    psnr = calc_psnr(img1, img2)
    print(f"{psnr:.4f}")
