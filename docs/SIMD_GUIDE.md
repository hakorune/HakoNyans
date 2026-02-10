# SIMD 実装ガイド

## 方針

| Tier | 命令セット | 対応状況 | 備考 |
|------|-----------|---------|------|
| **Tier 1** | AVX2 (x86) | 本線 | AMD Zen+ / Intel Haswell 以降 |
| **Tier 1** | NEON (ARM) | 本線 | Apple M1-M4 / Graviton / Snapdragon |
| **Tier 2** | AVX-512 (x86) | ボーナス | AMD Zen4+ / Intel Xeon |
| **Tier 3** | AVX10.2 | 将来 | Intel Nova Lake (2025後半〜) |
| N/A | SVE/SVE2 | 保留 | Apple 非対応、サーバーのみ |

## レジスタ幅と処理量

| 命令セット | レジスタ幅 | rANS 状態 (32-bit) 同時処理数 |
|-----------|-----------|-------------------------------|
| NEON | 128-bit | 4 |
| AVX2 | 256-bit | 8 |
| AVX-512 | 512-bit | 16 |

→ **N=8 インターリーブは AVX2 にぴったり**。NEON では 2 回に分けて処理。

## rANS デコードの SIMD 化ポイント

### 1. 状態更新 (最重要)

```cpp
// AVX2: 8 状態を同時更新
__m256i state = _mm256_loadu_si256(&states[0]);
__m256i freq  = _mm256_i32gather_epi32(cdf_table, symbols, 4);
// state = freq * (state / total) + (state % total) - bias
```

### 2. CDF ルックアップ

小アルファベット (数十種類) → **alias table** が最速:
- 1 回のテーブル引きで O(1)
- テーブルサイズが小さく L1 キャッシュに常駐

### 3. リノーマライズ

```cpp
// 条件付きバイト読み込み — 分岐回避が重要
// CMOV や blend 命令で分岐レスに
__m256i need_renorm = _mm256_cmpgt_epi32(lower_bound, state);
// ... conditional read ...
```

## ファイル構成

```
src/simd/
├── simd_dispatch.cpp       # runtime CPUID 検出
├── x86_avx2/
│   └── rans_decode_avx2.cpp
├── x86_avx512/
│   └── rans_decode_avx512.cpp
└── arm_neon/
    └── rans_decode_neon.cpp
```

## ディスパッチ戦略

```cpp
// 実行時に CPU 機能を検出して関数ポインタを設定
void init_simd() {
    if (cpu_has_avx512()) {
        rans_decode_fn = rans_decode_avx512;
    } else if (cpu_has_avx2()) {
        rans_decode_fn = rans_decode_avx2;
    } else {
        rans_decode_fn = rans_decode_scalar;
    }
}
```

## 開発順序

1. **スカラー C++ 実装** — 正しさの基準（golden reference）
2. **AVX2 実装** — 主戦場、ベンチマークの主軸
3. **NEON 実装** — Mac/ARM 対応
4. **AVX-512 実装** — ボーナス高速パス

## 注意事項

- AVX2 関数からの復帰前に `_mm256_zeroupper()` 必須
- NEON は 128-bit 固定 → N=8 を 2×4 で処理する設計に
- `HAKONYANS_FORCE_SCALAR=1` 環境変数で強制スカラーパス（A/B テスト用）
