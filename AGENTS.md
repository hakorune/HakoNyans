# AGENTS.md

この AGENTS.md は、箱理論の適用・コーディング・デバッグ・A/B 評価の共通言語です。
新しい最適化や経路を足す前に、まず箱と境界を設計してから手を動かします。

## Core Rule (Archive-by-Default)

- 運用は `opt-in` ではなく `archive-by-default` とする。
- 新規最適化・新経路・探索結果は、採用/不採用に関係なく記録を残す。
- 「不採用」も成果として保存し、同じ試行の再発を防ぐ。

## Box Workflow

1. 箱を定義する（目的、対象ステージ、非対象ステージ）。
2. 境界を定義する（フォーマット互換、サイズ非劣化、テスト要件）。
3. 実装する（差分は箱の内側だけに閉じる）。
4. A/B 評価する（固定条件、baseline 明示、artifact 保存）。
5. 判定する（promote / hold / archive）。

## Archive Policy

- 保存先は原則 `docs/archive/` とする。
- 各試行で最低限以下を残す。
  - 目的
  - 変更ファイル
  - 検証手順
  - 結果サマリ
  - 判定（採用/保留/不採用）と理由
- bench の CSV/ログは `bench_results/` に保存し、対応するアーカイブ文書から参照する。

## Links (入口)

- Claude入口: `CLAUDE.md`
- Claude作業ログ（アーカイブ）: `docs/archive/CLAUDE.md`
- hakonyans_boxes 入口: `hakonyans_boxes/README.md`
- hakonyans_boxes 研究箱アーカイブ: `hakonyans_boxes/archive/research/README.md`
- hakonyans_boxes P24アーカイブ（caged arena）: `hakonyans_boxes/archive/p24_caged_arena/PHASE_P24_CAGED_ARENA.md`
