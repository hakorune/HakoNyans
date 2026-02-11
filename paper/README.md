# HakoNyans Paper Workspace

This folder is dedicated to paper writing and reproducible measurements.
No feature development should happen here.

## Goal

Produce paper-ready evidence for:

- compression efficiency
- encode/decode speed
- quality retention

## Directory layout

- `manuscript.md`: main draft (paper text)
- `repro/`: reproducible measurement scripts
- `results/`: CSV/JSON/text outputs from scripts
- `figures/`: generated plots
- `tables/`: paper tables derived from `results/`

## Recommended "easy to understand" core figures

1. `Size vs Quality (PSNR)` curve
2. `Encode time vs Quality` line chart
3. `Decode time vs Quality` line chart
4. Category summary table (UI/Anime/Game/Photo/Natural)

## Minimal data set for paper

- UI: `test_images/ui/{browser,vscode,terminal}.ppm`
- Anime: `test_images/anime/{anime_girl_portrait,anime_sunset}.ppm`
- Game: `test_images/game/{minecraft_2d,retro}.ppm`
- Photo: `test_images/photo/{nature_01,nature_02}.ppm`
- Natural: `test_images/kodak/{kodim03,hd_01}.ppm`

## Quick start

From repository root:

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

## Paper build (TeX -> PDF)

Run this full sequence from repository root:

```bash
cd bench
../bench_png_compare | tee ../paper/results/lossless_png_compare_latest.txt
cd ..
python3 paper/repro/parse_lossless_png_compare.py
python3 paper/repro/run_lossy_jpeg_hkn_compare.py --runs 2
python3 paper/repro/generate_qualitative_figures.py
python3 paper/repro/generate_tex_tables.py
cd paper
latexmk -lualatex -interaction=nonstopmode paper_ja.tex
```

Output:

- `paper/paper_ja.pdf`

Or run one command:

```bash
bash paper/repro/build_paper.sh
```

## Paper freeze rule

During paper phase:

- Avoid changing codec behavior unless fixing correctness bugs.
- Keep experiment scripts and result files versioned in this folder.
