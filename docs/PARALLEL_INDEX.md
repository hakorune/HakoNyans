# P-Index: Decoder-Adaptive Parallel Index

## 概要

P-Index は NyANS-P ビットストリームに付加されるチェックポイント情報。
デコーダ側のコア数に応じて、**同一ファイルを任意の並列度で復号**できる。

参考: [Recoil: Parallel rANS Decoding (2023)](https://arxiv.org/pdf/2306.12141)

## チェックポイント構造

各チェックポイントは以下を記録：

| Field | Type | Description |
|-------|------|-------------|
| `byte_offset` | varint | CoreBitstream 内のバイト位置 |
| `token_index` | varint | トークン列内の何番目か |
| `states[N]` | uint32×N | rANS 中間状態 (N=interleave_n) |
| `context_id` | varint | 局所コンテキスト ID (optional) |

## 密度クラス

同一ファイルでも P-Index の密度を変えることで、異なるデコード環境に対応：

| Class | 間隔 | 用途 |
|-------|------|------|
| 0 | なし | シングルスレッドデコード |
| 1 | ~64KB | モバイル（2-4コア） |
| 2 | ~16KB | デスクトップ（8-16コア） |
| 3 | ~4KB | サーバー（32+コア） |

密度は `FileHeader.pindex_density` で指定。

## P-Index のエンコード

```
1. 通常通り interleaved rANS でエンコード
2. 一定間隔で rANS 状態をスナップショット
3. チェックポイントを差分 varint で圧縮して格納
```

## P-Index を使ったデコード

```
1. P-Index を読む
2. 利用可能なコア数 K を確認
3. チェックポイントから K 等分に近い分割点を選択
4. 各スレッドが独立した区間を復号：
   - byte_offset から CoreBitstream を読み開始
   - states[N] で rANS 状態を初期化
   - token_index 個目のトークンから復号
5. 全スレッドの結果を結合
```

## 差分 varint 圧縮

P-Index 自体のサイズを抑えるため、チェックポイントは差分で格納：

```
checkpoint[0]: absolute values
checkpoint[i]: delta from checkpoint[i-1]
  byte_offset_delta: varint
  token_index_delta: varint
  states[N]: raw uint32 (状態は差分圧縮しにくい)
```

## オーバーヘッド見積もり

N=8, 16KB 間隔の場合：
- チェックポイント 1 個 ≈ 2 (varint) + 2 (varint) + 32 (states) = 36 bytes
- 1MB 画像 → 約 64 チェックポイント → **~2.3KB** (0.2% 未満)

## 設計上の制約

- P-Index はデコーダ側の最適化であり、エンコーダは常に完全な情報を出力する
- チェックポイント間のコンテキストは独立（チャンク内で閉じる）
- P-Index がなくても逐次デコード可能（下位互換）
