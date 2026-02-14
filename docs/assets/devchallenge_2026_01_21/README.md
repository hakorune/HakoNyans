# Dev Challenge 2026-01-21 Image Pack

このフォルダは、dev.to 記事と GitHub README/Docs で共通利用する提出用画像セットです。

## 推奨掲載順（まずはこの5枚）

1. `fig01_photo_crop_strip.png`
- 用途: Photo 系の品質比較（冒頭の実力提示）

2. `fig03_ui_crop_strip.png`
- 用途: UI 系の比較（強み/弱みの説明に使いやすい）

3. `fig02_anime_crop_strip.png`
- 用途: Anime 系の比較（カテゴリ差の説明）

4. `fig04_vscode_baseline.png`
- 用途: Screen 系サンプル（baseline 側）

5. `fig05_vscode_screen.png`
- 用途: Screen 系サンプル（screen profile 側）

## 追加候補（必要なら）

- `fig06_browser_baseline.png`
- `fig07_browser_screen.png`
- `fig08_photo_full_strip.png`（高解像度の全幅版）
- `fig09_artoria_original_web.jpg`
- `fig10_artoria_hkn_web.jpg`
- `fig11_artoria_jpeg90_web.jpg`
- `fig12_artoria_compare_row_web.jpg`（3列横並び比較）
- `fig13_artoria_compare_crop_web.jpg`（拡大クロップ比較）
- `fig14_lossless_win_nature01_row_web.jpg`（ロスレス勝ちケース）
- `fig15_lossless_lose_hd01_row_web.jpg`（ロスレス負けケース）

## Anime 参考画像（Artoria）

- 元画像ファイル名:
  - `Artoria Pendragon (Tokyo Tower, Tokyo) by Takeuchi Takashi.png`
- 記事掲載向け軽量版:
  - `fig09_artoria_original_web.jpg`
  - `fig10_artoria_hkn_web.jpg`
  - `fig11_artoria_jpeg90_web.jpg`
  - `fig12_artoria_compare_row_web.jpg`
  - `fig13_artoria_compare_crop_web.jpg`
- 出典表記（記事中に明記）:
  - `Source: Fate series (Character: Artoria Pendragon), illustration by Takeuchi Takashi`

## dev.to 用 Markdown 例

```md
![Photo comparison crop](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig01_photo_crop_strip.png)

![UI comparison crop](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig03_ui_crop_strip.png)

![Anime comparison crop](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig02_anime_crop_strip.png)

![VSCode baseline](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig04_vscode_baseline.png)

![VSCode screen profile](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig05_vscode_screen.png)

![Artoria original (web)](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig09_artoria_original_web.jpg)

![Artoria HKN (web)](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig10_artoria_hkn_web.jpg)

![Artoria JPEG90 (web)](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig11_artoria_jpeg90_web.jpg)

![Artoria side-by-side compare](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig12_artoria_compare_row_web.jpg)

![Artoria crop compare](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig13_artoria_compare_crop_web.jpg)

![Lossless win case (nature_01)](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig14_lossless_win_nature01_row_web.jpg)

![Lossless lose case (hd_01)](https://raw.githubusercontent.com/hakorune/HakoNyans/main/docs/assets/devchallenge_2026_01_21/fig15_lossless_lose_hd01_row_web.jpg)
```

## 注意

- 速度/圧縮率の数値は画像ファイルサイズではなく、`bench_png_compare` の CSV を正とする。
- 記事本文では各図の直下に、対応する計測値（`size_bytes`, `Enc(ms)`, `Dec(ms)`, `PNG/HKN`）を併記する。
