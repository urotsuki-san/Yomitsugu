# iroha-kkc-v1

iroha のかな漢字変換モデル。今アプリが使っている iroha-t5-alpha（Release `kkc-model-v1`）の次の版です。

- 構成：文字単位のエンコーダ・デコーダ（T5、エンコーダ 12 層・デコーダ 2 層・幅 768、1 億 1,400 万パラメータ）。iroha-t5-alpha と同じ
- 学習データ：zenz-v2.5-dataset（Keita Miwa）
- 動かし方：llama.cpp（または iroha の MLX 版）

## ファイル

| ファイル | 量子化 | SHA-256 | バイト |
|---|---|---|---|
| iroha-kkc-v1-f16.gguf | f16 | 078a3ce62bba0d55f397ba728aafa55d192df464569ffe90c42c1a6e24346f74 | 228,103,936 |
| iroha-kkc-v1-Q8_0.gguf | Q8_0 | 8b974d37e8ed07e4792f011d0b1afcb90b99f5c95ca0716a837a421243ca3540 | 121,290,496 |
| iroha-kkc-v1-Q5_K_M.gguf | Q5_K_M | f060e259dbed28754f71e1634bc01a5d9d735d519f8ff72c333cac77c859efda | 81,535,360 |

## 正解率（acc@1）

読み制約あり・辞書なし・llama.cpp で測りました。

| 評価セット | 量子化 | iroha-kkc-v1 | iroha-t5-alpha | zenz-v3.1-small | zenz-v3.2-small |
|---|---|---|---|---|---|
| iroha-bench-v1（3,253 項目） | f16 | 69.4% | 68.3% | 67.8% | — |
| iroha-bench-v1 | Q5_K_M | 69.1% | 68.6% | 68.0% | 68.5% |
| AJIMEE-Bench（200 件） | f16 | 85.0% | 82.5% | 87.5% | — |
| AJIMEE-Bench | Q5_K_M | 86.0% | 82.0% | 87.0% | 85.5% |

評価セット iroha-bench-v1 は、リポジトリの `testdata/iroha-bench-v1/release/` にあります。評価の手順と集計のスクリプトも同じ場所に添えています。

## ライセンス

CC BY-SA 4.0（`iroha-kkc-v1-LICENSE` を参照）。学習データ zenz-v2.5-dataset のライセンスを継承しています。
