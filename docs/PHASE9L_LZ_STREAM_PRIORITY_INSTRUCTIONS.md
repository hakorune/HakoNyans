# Phase 9l: Tile-local LZ Integration (Priority Route)

## 0. Goal

UI/Anime lossless size gap against PNG is still large. Implement tile-local LZ in strict priority order:

1. `copy stream`
2. `block_types stream`
3. `palette stream` (mainly index payload)

Keep decode scalability and safety:
- Tile-local only
- 32KiB window
- Apply only when estimated/actual size improves by at least 2%
- Preserve existing decoding paths and old files

---

## 1. Current Evidence (Do not skip)

Recent diagnostics show:
- `vscode` lossless total: ~40KB
- dominant streams: `copy` (~28%), `block_types` (~19%), `palette` (~16%)
- palette dictionary is already effective (`dict_ref` high)

Conclusion:
- next gain is not color-table reuse itself
- next gain is repeated-sequence absorption in stream bytes

---

## 2. Scope and Order

## Phase 9l-1 (first)

LZ for `copy stream` only.

## Phase 9l-2 (second)

LZ for `block_types stream` only.

## Phase 9l-3 (third)

LZ for `palette stream` only (raw v2/v3 payload before optional compact envelope).

Do not implement all three at once. Land each phase independently with benchmarks.

---

## 3. Required File Changes

Minimum set:
- `src/codec/headers.h`
- `src/codec/encode.h`
- `src/codec/decode.h`
- `bench/bench_bit_accounting.cpp`
- `tests/test_lossless_round2.cpp`

Recommended new helper:
- `src/codec/lz_tile.h`

---

## 4. LZ Format (Fixed)

Use a simple byte-stream LZ wrapper, stream-local.

Wrapper (for each stream to compress):
- `[magic:u8][mode:u8][raw_count:u32][payload...]`

Magic values:
- copy stream wrapper: `0xA8`
- block_types stream wrapper: `0xA6` (existing compact magic; extend with new mode)
- palette stream wrapper: `0xA7` (existing compact magic; extend with new mode)

Modes:
- `mode=1`: existing rANS-byte-stream compact (already implemented for A6/A7)
- `mode=2`: new LZ payload

LZ payload token grammar:
- token tag `0`: LITRUN
  - `[tag=0][len:u8][len bytes literal]`
  - `len` in `[1..255]`
- token tag `1`: MATCH
  - `[tag=1][len:u8][dist:u16_le]`
  - `len` in `[3..255]`
  - `dist` in `[1..32768]`

Decode validity checks (mandatory):
- `produced <= raw_count`
- `dist <= produced`
- `produced + len <= raw_count`
- reject malformed/truncated payload safely

Window:
- sliding window max 32KiB per stream

---

## 5. Encoder Policy (Mandatory)

For each target stream in each tile:

1. Build original stream bytes (`raw`).
2. Build LZ payload (`lz`).
3. Compute wrapped size: `6 + lz.size()`.
4. Apply only if:
   - `wrapped_lz_size * 100 <= raw.size() * 98`  (>=2% smaller)
5. Otherwise keep existing representation.

Do not regress Photo/Natural due to overfitting.

---

## 6. Decoder Policy (Mandatory)

For each stream:
- First detect wrapper magic and mode.
- `mode=1`: keep existing decode path.
- `mode=2`: LZ-decode into `raw_count` bytes, then pass into existing stream decoder.
- If wrapper absent: decode as legacy raw stream.

Backward compatibility requirements:
- Old files must decode exactly as before.
- New files must decode with current decoder.

Versioning:
- bump file version only if needed for safety policy.
- if bumped, keep legacy decode path for previous versions.

---

## 7. Telemetry Additions

Add per-stream counters in `LosslessModeDebugStats` and print in `bench_bit_accounting`:

- `copy_lz_used_count`
- `copy_lz_saved_bytes_sum`
- `block_types_lz_used_count`
- `block_types_lz_saved_bytes_sum`
- `palette_lz_used_count`
- `palette_lz_saved_bytes_sum`

Also print adoption rate:
- used tiles / total tiles for each stream

---

## 8. Validation Commands

Build and tests:
- `cmake --build build -j8`
- `ctest --test-dir build --output-on-failure`

Mandatory benchmarks:
- `./build/bench_bit_accounting test_images/ui/vscode.ppm --lossless`
- `./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless`
- `./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless`
- `./build/bench_png_compare` (run from `build/`)
- `./build/bench_decode`

---

## 9. Acceptance Criteria (per sub-phase)

Common:
- `ctest` 17/17 pass
- no decode crash on malformed/truncated wrappers
- decode speed regression within +5%

Phase 9l-1 (copy LZ):
- UI average size improves >= 3%
- Photo average does not worsen by > 1%

Phase 9l-2 (block_types LZ):
- additional UI average improvement >= 1.5%
- no category regression > 2%

Phase 9l-3 (palette LZ):
- additional UI/Anime improvement >= 1%
- palette regression guard active (adoption not forced)

---

## 10. Commit Plan

Commit per phase (no squashing across phases):

1. `Phase 9l-1: Add tile-local LZ wrapper for copy stream`
2. `Phase 9l-2: Add tile-local LZ wrapper for block_types stream`
3. `Phase 9l-3: Add tile-local LZ wrapper for palette stream`

Each commit must include:
- changed files list
- tests result
- before/after benchmark table

---

## 11. Report Template

- Phase:
- Changed files:
- Algorithm summary:
- Test result (`ctest`):
- Bench (`vscode`, `anime_girl_portrait`, `nature_01`):
- `bench_png_compare` category deltas:
- Decode time delta:
- Risk / follow-up:
