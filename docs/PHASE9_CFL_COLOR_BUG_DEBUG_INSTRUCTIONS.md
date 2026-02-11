# Phase 9 CfL Color Bug Debug Instructions

## 0. Baseline (confirmed)
- Latest commit: `ebe761d` (`Phase 9k: finalize tile-match4 with decode guards and coverage`)
- Symptom:
  - `cfl=1` on anime image shows local color artifacts.
  - At Q75/Q90, `cfl=0` looks visually cleaner in user inspection.

## 1. Target
- Root-cause the color artifact when CfL is enabled.
- Keep compatibility and existing tests passing.
- Acceptable outcome:
  - `cfl=1` must not show obvious local hue shift vs `cfl=0`.
  - Quality metric with `cfl=1` should be close to or better than `cfl=0` at same setting.
  - Full regression suite still passes.

## 2. Repro (must run first)

Use the anime input:
- `test_images/anime/Artoria Pendragon (Tokyo Tower, Tokyo) by Takeuchi Takashi.png`

Prepare PPM input (if needed):
```bash
python3 - <<'PY'
from PIL import Image
img = Image.open("test_images/anime/Artoria Pendragon (Tokyo Tower, Tokyo) by Takeuchi Takashi.png").convert("RGB")
img.save("paper/results/color_check/artoria_in.ppm")
PY
```

Encode/decode matrix:
```bash
./hakonyans encode "paper/results/color_check/artoria_in.ppm" "paper/results/color_check/q75_444_cfl0.hkn" 75 0 0 0
./hakonyans decode "paper/results/color_check/q75_444_cfl0.hkn" "paper/results/color_check/q75_444_cfl0_out.ppm"

./hakonyans encode "paper/results/color_check/artoria_in.ppm" "paper/results/color_check/q75_444_cfl1.hkn" 75 0 1 0
./hakonyans decode "paper/results/color_check/q75_444_cfl1.hkn" "paper/results/color_check/q75_444_cfl1_out.ppm"

./hakonyans encode "paper/results/color_check/artoria_in.ppm" "paper/results/color_check/q75_420_cfl0.hkn" 75 1 0 0
./hakonyans decode "paper/results/color_check/q75_420_cfl0.hkn" "paper/results/color_check/q75_420_cfl0_out.ppm"

./hakonyans encode "paper/results/color_check/artoria_in.ppm" "paper/results/color_check/q75_420_cfl1.hkn" 75 1 1 0
./hakonyans decode "paper/results/color_check/q75_420_cfl1.hkn" "paper/results/color_check/q75_420_cfl1_out.ppm"
```

## 3. Primary hypotheses (check in order)

### H1. Encoder/decoder predictor model mismatch (highest priority)
Potential mismatch points:
- `src/codec/encode.h` around CfL residual generation (`alpha_q8` centered predictor).
- `src/codec/encode.h` legacy payload serialization (`a_q6`, `b_legacy` conversion).
- `src/codec/decode.h` legacy parse path applies `pred = a*y + b`.

Why suspicious:
- Encode uses Q8-centered predictor for residual.
- Bitstream currently serialized as legacy `(a_q6, b_legacy)` for compatibility.
- Decode reconstructs with legacy formula and quantized slope.
- Small model mismatch can become visible after quantization.

### H2. CfL decision gate is not RD-aware enough
Current selection checks raw block-domain MSE reduction, but not final coded cost/quantized distortion.
This can over-apply CfL on blocks where chroma residual becomes harder after quantization.

### H3. 4:2:0 path alignment/strength issue
Need to separate:
- artifact present in `4:4:4 + cfl=1`?
- artifact gets worse in `4:2:0 + cfl=1`?
If yes for 4:4:4 too, root cause is CfL core (not only subsampling).

## 4. Required instrumentation

Add temporary debug counters/logs (remove or gate behind macro before final commit):
- CfL applied block count per plane.
- Distribution of `alpha_q8` (min/max/median) and `beta`.
- Count of blocks where:
  - `mse_cfl < mse_no_cfl - 1024` (selected),
  - but post-quant estimate is worse (if you add estimate).
- Max absolute predictor mismatch between:
  - encoder-side predictor used for residual,
  - decoder-side predictor implied by serialized params.

Suggested metric dump (CSV):
- `bx,by,plane,cfl_applied,alpha_q8,beta,mse_no_cfl,mse_cfl,pred_diff_max`

## 5. Fix plan (priority)

### Fix A (must do): make residual predictor match bitstream predictor exactly
When using legacy payload:
- Compute residual with the same quantized `a_q6` and legacy-equivalent predictor that decoder uses.
- Do not compute residual with higher-precision model if you serialize lower-precision params.
Goal: eliminate encode/decode predictor drift.

### Fix B (recommended): improve CfL application decision
Replace pure MSE gate with simple RD-like gate:
- Estimate residual energy and signaling cost.
- Apply CfL only if estimated total bits+distortion improves.
At minimum:
- tighten threshold adaptively (quality-dependent),
- add guard on very low luma variance and extreme `|alpha|`.

### Fix C (safety): fallback guards
If block is risky, force CfL off:
- low luma variance,
- high chroma residual after prediction,
- unstable alpha (near clamp boundary).

## 6. Validation checklist

### Functional
```bash
ctest --output-on-failure
```
Must remain all-pass.

### Visual/quality
- Re-run the 4-case matrix in section 2.
- Confirm artifact area is removed or clearly reduced in `cfl=1`.
- Compare `q75_444_cfl1` vs `q75_444_cfl0` and `q75_420_cfl1` vs `q75_420_cfl0`.

### Bench smoke
```bash
./bench_png_compare
./bench_bit_accounting test_images/photo/nature_01.ppm --lossy --quality 50
```
No major regression in decode speed or size.

## 7. Expected deliverables from Gemini
1. Root cause summary (H1/H2/H3 which one, with evidence).
2. Patch list with touched files.
3. Before/after table:
   - file size,
   - PSNR,
   - decode time,
   - artifact status (visual).
4. Regression status (`ctest` pass count).
5. If trade-off exists, clearly state default policy (quality-first vs size-first).

## 8. Notes
- Keep wire compatibility unless version bump is explicitly proposed.
- If format change is required, add guarded decode path and version gate.
- Prioritize correctness (artifact removal) over small size gain.
