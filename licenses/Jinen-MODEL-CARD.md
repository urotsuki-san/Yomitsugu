---
license: cc-by-sa-4.0
language:
- ja
pipeline_tag: text-generation
tags:
- gguf
- japanese
- kana-kanji-conversion
---

# jinen-v2-xsmall (GGUF)

かな漢字変換モデル jinen v2 の GGUF 版 (xsmall / 35.7M)。
**NFKC 前提**のモデルなので、プロンプトは NFKC 正規化して投げること。

## AJIMEE-Bench

[azooKey/AJIMEE-Bench](https://github.com/azooKey/AJIMEE-Bench) `JWTD_v2/v1`
200 問、greedy。

| ファイル | サイズ | Accuracy@1 | Accuracy@1 (NFKC) | p50 | p90 | p99 |
|---|---:|---:|---:|---:|---:|---:|
| `jinen-v2-xsmall-f16.gguf` | 72 MB | 73.5% | 78.5% | 20 ms | 44 ms | 69 ms |
| `jinen-v2-xsmall-Q8_0.gguf` | 39 MB | 73.5% | 78.5% | 12 ms | 26 ms | 43 ms |
| `jinen-v2-xsmall-Q5_K_M.gguf` | 28 MB | 74.0% | 79.0% | 13 ms | 29 ms | 45 ms |
| `jinen-v2-xsmall-Q4_K_M.gguf` | 26 MB | 73.0% | 78.0% | 11 ms | 24 ms | 42 ms |

NFKC 列は参照側にも NFKC を掛けたもの。
llama.cpp b10200 (CPU, 4 threads)、`n_ctx 1024`、左文脈 64 文字。
参考: 変換元の HF 版 (PyTorch) は exact 73.0% / NFKC 78.0%。

## 使い方

```bash
llama-server -m jinen-v2-xsmall-Q5_K_M.gguf --temp 0 --top-k 1
```

プロンプト形式は [jinen-v1](https://huggingface.co/togatogah/jinen-v1-small) と同じで、
特殊トークンで組み立てる。

| トークン | 役割 |
|---|---|
| `\uee00` | 入力 (読み) の開始 |
| `\uee01` | 出力 (変換結果) の開始 |
| `\uee02` | 左文脈の開始 |

```python
import unicodedata
INPUT_START, OUTPUT_START, CONTEXT = "\uee00", "\uee01", "\uee02"

prompt = f"{CONTEXT}{context}" if context else ""
prompt += f"{INPUT_START}{reading}{OUTPUT_START}"
prompt = unicodedata.normalize("NFKC", prompt)  # 必須。入れないと精度が大きく落ちる
```

`\uee01` 以降を変換結果として読み、**必ず greedy (`--temp 0 --top-k 1`)** で使うこと。

## 出典

学習コーパスの一部として、国立国会図書館の書誌データを加工しました。

> 出典：国立国会図書館「全国書誌データ」（2026年7月9日に取得）および
> 国立国会図書館サーチの書誌データを加工して作成

加工および本モデルの作成は Hitoshi Togasaki によるものであり、
国立国会図書館が作成・提供・保証するものではありません。

## ライセンス

[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/deed.ja)
