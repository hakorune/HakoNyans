# Box Theory for HakoNyans

## 箱理論（Box Theory）— AI協働設計の哲学

### 1. 箱理論の核心

```
┌─────────────────────────────────────────────────────────┐
│  箱（Box）は「人間のための境界」≠「CPUの境界」            │
│                                                          │
│  人間が理解・設計・デバッグするためのソースコード構造      │
│  コンパイル後に融合（inline/LTO）しても構わない            │
└─────────────────────────────────────────────────────────┘
```

### 2. 5つの原則（AI協働の合言葉）

| # | 原則 | 具体例（HakoNyans向け） |
|---|------|------------------------|
| 1 | 箱にする | エントロピー符号化、変換（DCT/Wavelet）、パッキング を役割ごとに箱分離 |
| 2 | 境界を作る | ANS エンコード → ビットストリーム、デコード → シンボル列 の変換点を1箇所に |
| 3 | 戻せる | `HAKONYANS_FORCE_C` 環境変数で新旧経路をA/B可能に |
| 4 | 見える化 | ワンショットログ、圧縮率統計でボトルネック把握 |
| 5 | Fail-Fast | エンコード失敗/展開不可は即時エラー（不完全圧縮ファイル禁止） |

### 3. 人間の境界 vs プログラムの境界

| 区分 | 説明 | 実現手法 |
|------|------|---------|
| **人間の境界** | 役割/責務/データ所有を整理するための構造 | モジュール設計、API境界 |
| **プログラムの境界** | CPUが見る境界（最適化後） | static inline、-flto、-fno-plt |

**重要ルール**
- 箱は壊さない → まず箱で整理 → 必要なら「ビルドで融合する」
- 「強制fuse」（内部シンボルをヘッダ直参照）は最後の手段

### 4. HakoNyans の層構造と責務

```
L0: Bitstream Box（最下層）
    └─ バイト境界、ビット単位アクセス
       API: read_bits(n) / write_bits(v, n)

L1: Symbol Box（中層下）
    └─ エントロピー符号化前後のシンボル列
       API: encode_symbol(sym) / decode_symbol() 

L2: ANS Entropy Box（中層）
    └─ rANS/tANS符号化と復号の秩序付け
       API: ans_encode() / ans_decode()

L3: Transform Box（中層上）
    └─ 色変換、DCT/Wavelet、量子化
       API: transform_forward() / transform_inverse()

L4: Frame Box（上層）
    └─ フレーム構造、メタデータ、フレーム間予測
       API: encode_frame() / decode_frame()
```

### 5. 実装規約（C向け）

```c
// static inlineで箱間オーバーヘッドをゼロに
static inline uint32_t bitstream_read_bits(struct bitstream *bs, int n) {
  // ...
}

// 共有状態は _Atomic で明示
_Atomic(uint32_t) ans_state;

// 箱間のAPI呼び出しは最小限
// 変換点（ボックス境界）は明確に
struct symbol_buffer {
  int16_t *data;      // Transform Box からの入力
  size_t count;
};
```

### 6. NO-GO時の運用ポリシー

実験的な箱が上手くいかなかった場合の対応：

1. **ソース移動**: `research/<box_name>/` に移動
2. **公開パスはstub化**: 元のパスは機能OFFに
3. **ビルドから除外**: CMakeLists.txt から削除
4. **台帳更新**: docs/ と CURRENT_TASK.md に記録

**凍結箱の例**
| 箱 | 状態 | 備考 |
|----|------|------|
| ANS v1（fixed model） | NO-GO | adaptive model へ移行 |
| Wavelet transform | 凍結（研究用） | DCT採用でデフォルトOFF |

### 7. チェックリスト（PR/レビュー時）

- [ ] 箱の境界は明確か（変換点が1箇所に集約）
- [ ] フラグで戻せるか（A/Bが即時可能）
- [ ] 可視化のフックは最小か（ワンショット or カウンタ）
- [ ] Fail-Fastになっているか（誤魔化しフォールバックなし）
- [ ] C では static inline でオーバーヘッドを消しているか

### 8. HakoNyans 適用例

**デコード流れ（箱の連鎖）**

```
Bitstream Box       ビットストリーム読み込み
     ↓ (read_bits)
Symbol Box          ANS シンボル列
     ↓ (decode_symbol)
ANS Entropy Box     統計テーブル、状態遷移
     ↓ (ans_decode)
Transform Box       量子化された係数
     ↓ (transform_inverse)
Frame Box           ピクセル値
     ↓ (frame_output)
RGB/YUV 画像
```

各矢印は API 境界 → テスト・入れ替え・検証が容易

---

**要するに**: 「すべてを箱で分けて、いつでも戻せるように積み上げる」設計哲学
