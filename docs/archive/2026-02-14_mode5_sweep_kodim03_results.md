# Mode5 Sweep Results (2026-02-14)

## Sweep Configuration
- Lane: max
- Runs: 3 (warmup=1)
- Parameters swept:
  - gain_permille: 990, 995, 1000
  - min_raw_bytes: 256, 512, 1024
  - min_lz_bytes: 64, 128, 256
- Total combinations: 27

## Results Summary

### All Combinations Identical
All 27 parameter combinations produced **exactly the same results**:
- total_hkn_bytes: 2,977,509
- kodim03_bytes: 369,844
- median PNG/HKN: 0.325572

### kodim03 Analysis
From bit_accounting observation:
```
mode5_candidates: 3
rej_gate: 3/0
avg_bytes: LZ=388423.7 / Wrap=257074.0 / Leg=148395.3
```

**Why Mode5 doesn't help kodim03:**
- Legacy size: ~148KB
- Mode5 (LZ+rANS) size: ~257KB
- LZ alone size: ~388KB

Mode5 wrapped output is **larger than legacy**, so it's correctly rejected.
The LZ step is not finding good matches for kodim03's content.

## Conclusion

Mode5 parameter tuning **does not improve kodim03**.
The issue is not the selection gate - it's that LZ compression
is not effective on this image's filter_lo content.

## Next Steps

To improve kodim03, consider:
1. Improve LZ match finding (better hash, longer window)
2. Add different filter modes for kodim03-like content
3. Improve filter selection before LZ step
4. Explore PNG's Paeth+LZ77 combination more deeply
