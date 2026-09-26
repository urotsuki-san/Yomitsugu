# 同梱ソフトウェアと辞書のライセンス

Yomitsuguの独自コードは[MIT](LICENSE)です。辞書・モデル・同梱ライブラリーには、以下のライセンスが適用されます。配布パッケージには各ライセンスの本文を収録しています。

| 構成要素 | 用途 | ライセンス・出典 | 表記の保存先 |
|---|---|---|---|
| [myime](https://github.com/unok/myime)（`a8486eca5312556ff88fed7f1850a28843b67977`） | 変換エンジンの接続とWindowsビルド | MIT。依存物にはそれぞれのライセンスを適用 | ビルド元の `upstream/myime/LICENSE`。配布時は `licenses/myime-LICENSE` |
| [AzooKeyKanaKanjiConverter](https://github.com/azooKey/AzooKeyKanaKanjiConverter) | かな漢字変換 | MIT | 上流の `LICENSE` を配布パッケージに同梱 |
| [azooKey_dictionary_storage](https://github.com/azooKey/azooKey_dictionary_storage) | 本辞書 | Apache-2.0 | 配布時は `licenses/azookey-dictionary-LICENSE` |
| [azooKey_emoji_dictionary_storage](https://github.com/azooKey/azooKey_emoji_dictionary_storage) | AzooKeyの絵文字辞書 | Mozc・Unicode emoji・CLDR由来。各出典の条件を適用 | 上流の `README.md` と `data/README.md`、MozcとUnicodeの表記を同梱 |
| [Zenzai v3.2 small GGUF](https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf)（`c67e03e07d215c869f591b274c1631170d3e11fe`） | ローカルモデル。SHA-256 `29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673` | モデルのリポジトリ表記はApache-2.0 | モデルの出典とApache-2.0の本文を同梱 |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | myimeが使うモデル実行基盤 | MIT | 配布パッケージに同梱 |
| [Swift runtime and Foundation](https://www.swift.org/) | 変換プロセス内のSwift・Foundation・Dispatch DLL | Apache-2.0とSwift Runtime Library Exception。Foundation内のUnicodeデータはUnicode License | `licenses/swift-LICENSE` と `licenses/unicode-LICENSE` |
| Microsoft Visual C++ runtime | `vcruntime`, `msvcp`, `concrt` DLLs | 使用したVisual Studioの再配布条件 | Microsoftの[再配布可能ファイル一覧](https://learn.microsoft.com/en-us/cpp/windows/determining-which-dlls-to-redistribute?view=msvc-170) に従いRelease用DLLを同梱 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSONの読み書き | MIT | `native/third_party/nlohmann/LICENSE.MIT` |
| [Mozc `symbol.tsv`](https://github.com/google/mozc/blob/master/src/data/symbol/symbol.tsv) | 公開辞書の記号 | BSD-3-Clause。付属の著作権表示を保持 | `native/third_party/public-dictionary-NOTICE.txt` |
| [Mozc romanization table](https://github.com/google/mozc/blob/b9c3fcbd6d76b19649ef572324fa9da2559bc18e/src/data/preedit/romanji-hiragana.tsv) | 拡張音と子音の持ち越しを含むローマ字の変換規則 | BSD-3-Clause | `native/third_party/public-dictionary-NOTICE.txt`; 出典のリビジョンとSHA-256は `native/src/romaji_table.inc` |
| [EDRDG EDICT2](https://www.edrdg.org/pub/Nihongo/edict2u.gz) | 一般語・外来語・コンピューター用語と英字保持用の索引 | CC BY-SA 4.0。派生辞書にも表示・継承の条件を適用 | `native/third_party/public-dictionary-NOTICE.txt` |

公開辞書 `native/assets/public_dictionary.tsv` は複数の出典をまとめたデータです。EDRDG由来の部分にはCC BY-SA 4.0を適用します。コードのMITライセンスで辞書の条件を置き換えるものではありません。更新時には出典と取得ファイルのSHA-256を `.sources.json` に保存します。

アーキテクチャ図に埋め込まれたJetBrains MonoはSIL Open Font License 1.1です。ライセンス本文は `docs/architecture/yomitsugu.html` に含まれています。

変換処理の変更は [myimeへのパッチ](patches/myime-context-conversion.patch)に収録しています。前後文脈の受け渡し、候補の探索・順位付け、読みと送り仮名の照合が対象です。固定リビジョンへ適用してビルドし、myimeとAzooKeyのライセンス本文は配布物にも保持します。

品質比較には [AJIMEE-Bench](https://github.com/azooKey/AJIMEE-Bench) を使っています。評価データはCC BY-SA 3.0、ツールのコードはCC0です。評価データはインストーラーや辞書には含めていません。
