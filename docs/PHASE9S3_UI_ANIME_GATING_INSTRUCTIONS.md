# Phase 9s-3: UI/Anime Gating + Fallback Tuning (Screen-profile v1.5)

## 0. Goal
`screen-profile v1` は UI で有効だが、Anime の一部（例: `anime_sunset`）で未達が残っている。

このフェーズでは、`screen-indexed` 採用条件を強化し、誤採用を減らす。

- 目標1: Anime カテゴリ平均を `PNG比 +10% -> +5% 以下` に改善
- 目標2: UI カテゴリ平均 `PNG比 -30%` 以上を維持
- 目標3: Photo カテゴリの悪化を禁止（中央値 +1% 以内）
- 目標4: デコード性能劣化を `-5%` 以内に抑制

---

## 1. Root Cause (現状の仮説)

1. `screen-indexed` の採用判定が平面的（サイズ比較のみ）で、Anime の高周波タイルで過剰適用される。
2. `screen-indexed` 内部で payload mode=0(raw) を選ぶケースは、実質圧縮なしでメタだけ増える。
3. 現在のログでは「なぜ採用/不採用になったか」の情報が不足しており、しきい値調整の再現性が低い。

---

## 2. Implementation Scope

### 2.1 `src/codec/encode.h`: screen-indexed 採用判定を 2段化

`encode_plane_lossless()` の末尾（`screen-indexed` 候補を試す箇所）を以下の判定に置換:

1. **Pre-gate (構造的に不利なタイルを除外)**
- `use_photo_mode_bias == true` は即除外（現行維持）
- `width * height < 4096` は除外（現行維持）
- `palette_count > 48` は除外
- `bits_per_index > 6` は除外
- `screen_mode == 0(raw)` かつ `raw_packed_size > 2048` は除外

2. **Cost-gate (カテゴリ別しきい値)**
- `ui_like` 推定: `palette_count <= 24 && bits_per_index <= 5`
- `anime_like` 推定: `palette_count > 24 || bits_per_index == 6`
- 採用条件:
  - `ui_like`: `screen_size * 100 <= legacy_size * 99`（1%以上改善で採用）
  - `anime_like`: `screen_size * 100 <= legacy_size * 97`（3%以上改善で採用）

`ui_like/anime_like` は暫定ヒューリスティックで良い。重要なのは「Anime 側を保守的にする」こと。

### 2.2 `src/codec/encode.h`: debug telemetry を追加

`LosslessModeDebugStats` に以下を追加:

- `screen_candidate_count`
- `screen_selected_count`
- `screen_rejected_pre_gate`
- `screen_rejected_cost_gate`
- `screen_mode0_reject_count`
- `screen_ui_like_count`
- `screen_anime_like_count`
- `screen_palette_count_sum`
- `screen_bits_per_index_sum`
- `screen_gain_bytes_sum`（`legacy_size - screen_size` の正値加算）
- `screen_loss_bytes_sum`（不採用時 `screen_size - legacy_size` の正値加算）

### 2.3 `bench/bench_bit_accounting.cpp`: telemetry 表示追加

lossless 出力に以下を追加:

- `screen_candidate_count`
- `screen_selected_count`
- `screen_rejected_pre_gate`
- `screen_rejected_cost_gate`
- `screen_mode0_reject_count`
- `screen_ui_like_count`
- `screen_anime_like_count`
- `screen_avg_palette_count`
- `screen_avg_bits_per_index`
- `screen_gain_bytes_sum`
- `screen_loss_bytes_sum`

### 2.4 `tests/test_lossless_round2.cpp`: 回帰テスト追加

最低2件追加:

1. `test_screen_indexed_anime_guard()`
- 疑似的に色数多め + 反復弱めのパターンを生成
- roundtrip が bit-exact
- `screen-indexed` を強制しない（guard が働く）ことを確認

2. `test_screen_indexed_ui_adopt()`
- 少色 + 反復強いパターン
- roundtrip が bit-exact
- `screen-indexed` が採用されることを確認

テスト実装は既存の roundtrip テスト方式に合わせること。

---

## 3. Validation Steps (必須)

### 3.1 Build + Tests

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

成功条件: `17/17 PASS`（または追加分を含む全PASS）

### 3.2 Bit Accounting (3カテゴリ)

```bash
./build/bench_bit_accounting test_images/ui/vscode.ppm --lossless
./build/bench_bit_accounting test_images/anime/anime_girl_portrait.ppm --lossless
./build/bench_bit_accounting test_images/anime/anime_sunset.ppm --lossless
./build/bench_bit_accounting test_images/photo/nature_01.ppm --lossless
```

確認ポイント:
- UIでは `screen_selected_count` が増える/維持
- Animeでは `screen_rejected_cost_gate` が増え、過剰採用が減る
- Photoでは `screen_candidate_count` がほぼ0（または不採用）

### 3.3 PNG Compare

```bash
cd build
./bench_png_compare | tee ../bench_results/phase9s3_png_compare.txt
cd ..
```

確認ポイント:
- UIカテゴリ平均: 既存優位を維持
- Animeカテゴリ平均: 改善（+10% -> +5%以下）
- Photoカテゴリ: 悪化なし

### 3.4 Decode Perf

```bash
./build/bench_decode | tee bench_results/phase9s3_decode.txt
```

確認ポイント:
- Throughput が前回比 -5%以内

---

## 4. DoD (Definition of Done)

- [ ] `ctest` 全PASS
- [ ] UIカテゴリ平均: `PNG比 <= -30%` 維持
- [ ] Animeカテゴリ平均: `PNG比 <= +5%`
- [ ] Photoカテゴリ: `+1%` 以内
- [ ] decode throughput 低下が `5%` 以内
- [ ] `bench_results/phase9s3_png_compare.txt` を保存
- [ ] `bench_results/phase9s3_decode.txt` を保存

---

## 5. Commit Message

```text
Phase 9s-3: tune screen-indexed gating for UI/Anime and add fallback telemetry
```

---

## 6. Report Format (Gemini返答テンプレ)

以下の形式で返答すること:

1. 変更ファイル一覧
2. 実装要点（pre-gate / cost-gate / telemetry）
3. テスト結果（PASS数）
4. ベンチ結果（UI/Anime/Photoカテゴリ差分）
5. デコード速度差分
6. 回帰有無と未達項目

