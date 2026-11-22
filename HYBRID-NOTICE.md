# ハイブリッド変換の出典

Yomitsuguの独自コードはMITです。元のランタイム・辞書の著作権表示は licenses/Yomitsugu-THIRD-PARTY.md および licenses/ 以下にあります。変換エンジンで使うコードとモデルの出典を記します。

- [iroha](https://github.com/TetsuakiBaba/iroha/tree/b6e018189e8f4e006d0d7749552c2fa7a7f62852) のWindows変換器を使用。著作権者 Tetsuaki Baba、MIT。Yomitsugu側でJinen用プロンプトの正規化と文脈長、物理バッチサイズ、候補探索と採点を変更しました。改変を含むコードはリポジトリの `native/src/hybrid/` と `native/third_party/iroha/` にあります。
- [llama.cpp](https://github.com/ggml-org/llama.cpp/tree/57291f2644af8c9df0dd8d44395881c5bdcf0ecd) のCPU静的ビルド、MIT。irohaが配布するT5交差注意の再利用・Zenz用トークナイザーのパッチを適用しました。
- Zenz v3.2 small は Miwa-Keita が配布する Q5_K_M です。固定したリポジトリのライセンス表記は Apache-2.0 です。本文は `licenses/zenzai-model-LICENSE` にあります。[配布元のモデルカード](https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/blob/c67e03e07d215c869f591b274c1631170d3e11fe/README.md)
- [Zenz v3.2 xsmall](https://huggingface.co/Miwa-Keita/zenz-v3.2-xsmall-gguf/tree/42cab8392a3727d1cf80932cf2275b7d523fbb9f)、Miwa-Keita、Q5_K_M、CC BY-SA 4.0。
- [Jinen v2 xsmall](https://huggingface.co/togatogah/jinen-v2-xsmall.gguf/tree/3910fd01bf4ba86eca89617f18db9b0c1c5b2283)、togatogah、Q5_K_M、CC BY-SA 4.0。学習データ等の帰属は licenses/Jinen-MODEL-CARD.md を参照してください。
- [iroha-kkc-v1](https://github.com/TetsuakiBaba/iroha/releases/tag/iroha-kkc-v1)、Tetsuaki Baba、Q5_K_M、CC BY-SA 4.0。学習元はKeita Miwaのzenz-v2.5-dataset。モデルカード・ライセンス本文を同梱しています。

4つのGGUFは各作者の配布ファイルをそのまま収録しています。Zenz v3.2 xsmall、Jinen v2 xsmall、iroha-kkc-v1のライセンスはCC BY-SA 4.0です。[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

英字名称の索引には [Simple Icons 16.38.0](https://github.com/simple-icons/simple-icons/tree/16.38.0) の名称データ（CC0）と補足語を使っています。配布条件は `licenses/Simple-Icons-CC0.md` に収録しています。
