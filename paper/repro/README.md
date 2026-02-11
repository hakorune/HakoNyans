# Repro Scripts

## Scripts

- `run_lossy_rd.py`
  - Measures lossy size/PSNR/encode/decode and writes CSV
- `run_lossless_snapshot.sh`
  - Captures current lossless benchmark outputs

## Typical usage

```bash
python3 paper/repro/run_lossy_rd.py \
  --bin ./build/hakonyans \
  --qualities 30,50,70,90 \
  --runs 3 \
  --out-csv paper/results/lossy_rd.csv
```

```bash
bash paper/repro/run_lossless_snapshot.sh
```
