import subprocess
import re

images = [
    "test_images/kodak/kodim01.ppm",
    "test_images/kodak/kodim02.ppm",
    "test_images/kodak/kodim03.ppm",
    "test_images/kodak/hd_01.ppm",
    "test_images/photo/nature_01.ppm",
    "test_images/photo/nature_02.ppm"
]

for img in images:
    print("\n--- " + img + " ---")
    res = subprocess.run(["./build/bench_bit_accounting", img, "--lossless"], capture_output=True, text=True)
    for line in res.stdout.splitlines():
        if "natural_prefilter_avg" in line or "natural_prefilter  pass/rej" in line:
            print(line.strip())
        if "natural_candidates" in line:
            print(line.strip())
        if "natural_selected" in line:
            print(line.strip())
        if "natural_loss_bytes" in line or "natural_gain_bytes" in line:
            print(line.strip())
