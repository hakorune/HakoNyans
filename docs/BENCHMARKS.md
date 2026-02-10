# ベンチマーク方針

## 原則

- **I/O を除外して計測** — ファイル読み書きは別
- **最低 10 回実行**、中央値を採用
- **A/B テスト可能** — 環境変数で SIMD/スカラー切り替え

## 計測対象

### 1. bench_entropy (最優先)

NyANS-P エントロピー層単体のスループット。

```
指標: MiB/s (デコード速度)
入力: ランダム生成トークン列 / 実画像から抽出した係数
比較: zstd, lz4, Huffman (baseline)
```

**これが勝てなければ上位層を作る意味がない。**

### 2. bench_decode

エンドツーエンドのデコード速度。

```
指標: ms/frame, FPS
入力: .hkn ファイル (各解像度)
比較: libjpeg-turbo, libavif, libjxl
```

### 3. bench_encode

エンコード速度（デコード優先なので後回し）。

## テスト画像セット

| 名前 | 解像度 | 用途 |
|------|--------|------|
| test_small | 256×256 | 単体テスト・CI |
| test_hd | 1920×1080 | Full HD ベンチ |
| test_4k | 3840×2160 | 4K ベンチ |
| test_medical | 各種 | 12-bit/16-bit 高精度 |

## 環境変数

| 変数 | 効果 |
|------|------|
| `HAKONYANS_FORCE_SCALAR=1` | SIMD 無効化 |
| `HAKONYANS_THREADS=N` | デコードスレッド数指定 |
| `HAKONYANS_PINDEX=0` | P-Index 無効化（逐次デコード強制） |

## 計測コマンド例

```bash
# エントロピー層ベンチ
./bench_entropy --iterations 100 --input corpus/tokens_hd.bin

# エンドツーエンド
./bench_decode --iterations 10 --input test_hd.hkn

# perf プロファイリング
perf record -g ./bench_decode --iterations 1 --input test_4k.hkn
perf report --no-children --stdio --percent-limit=1
```

## 目標値 (暫定)

| 指標 | 目標 | 根拠 |
|------|------|------|
| エントロピーデコード | >500 MiB/s (1コア) | rANS + LUT で到達可能 |
| Full HD デコード | <15ms | libjpeg-turbo 8-bit (~20ms) より速く |
| 4K デコード | <50ms | 並列デコードで達成 |
