<div align="center">

# Yomitsugu

### ローマ字を打ち続け、その場で日本語へ。

An experimental Windows IME for continuous Roman-letter typing and local Japanese conversion.

<img src="docs/assets/readme/yomitsugu-showcase-hero-v2.png" alt="Yomitsugu showcase: a dark pixel-art typesetter scene with the Yomitsugu title" width="100%">

<p>
  <img alt="Status: preview" src="https://img.shields.io/badge/status-preview-7c3aed?style=for-the-badge">
  <img alt="Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-334155?style=for-the-badge">
  <img alt="Local conversion" src="https://img.shields.io/badge/conversion-local-0f766e?style=for-the-badge">
  <a href="LICENSE"><img alt="License: MIT for original code" src="https://img.shields.io/badge/code-MIT-0f766e?style=for-the-badge"></a>
</p>

**[現在の状態](#現在の状態)** · **[入力例](#入力例)** · **[仕組み](#仕組み)** · **[開発と検証](#開発と検証)** · **[ライセンス](#ライセンスと出典)**

</div>

> [!IMPORTANT]
> **これは開発中のソース公開版です。一般向けのインストーラーや安定版バイナリはまだありません。**
>
> Rich Editの単一テストホストでは動作を確認しましたが、Chrome・Office・VS Codeなどでの互換性、インストール更新、アクセシビリティ、同梱バイナリ全体の権利確認は完了していません。ここに公開リポジトリがあることと、誰にでも配れるWindowsパッケージが完成したことは別です。

Yomitsuguは、ローマ字と英語が混ざった入力を文脈の中で扱う、Windows Text Services Framework（TSF）の入力システムです。入力中の候補表示、かな漢字変換、ユーザー辞書、記号辞書の更新をひとつの流れに収めることを目指しています。名前は「読みを継ぐ」から。短い語だけでなく、文の途中でも読みと変換をつなぎたい、という設計意図を表しています。

## 入力例

| 入力 | 現在の候補・動作 |
|---|---|
| `samukunaltutekimasitane` | 寒くなってきましたね |
| `ri-domi-` | README（公開辞書を使用） |
| `ri-domi-wokousinnsitekudasaiGithubde` | READMEを更新してくださいGithubで |
| `sannkai` | 散開を先頭候補に表示。三回・3階なども候補に残す |
| `toritatigasannkaisuru` | 鳥たちが散開するを候補に表示（現状は2番目） |
| `yajirushi` | →（公開辞書を使用） |
| `ltu` / `a` / `.` | っ / あ / 。 |

上の結果は辞書とテスト環境に依存します。候補を確定するのはユーザーです。長文・未知語・アプリごとの入力欄では誤変換が残ります。

## 現在の状態

- **0.2.7-preview は個人評価段階。** 変換修正はネイティブ回帰と単一Rich Editホストの実機E2Eを通過しました。一般配布版ではありません。
- 日本語候補はAzooKeyの辞書とローカルのZenzaiモデルを専用のエンジンプロセスで使います。入力先アプリへSwiftランタイムを直接読み込ませません。
- 公開辞書は[Mozcの記号データ](https://github.com/google/mozc/blob/master/src/data/symbol/symbol.tsv)と[EDRDG EDICT2](https://www.edrdg.org/pub/Nihongo/edict2.gz)から生成します。更新は利用者の操作で辞書ファイルだけを取得し、入力文や個人辞書は送信しません。オフラインでも同梱辞書を使えます。
- 設定画面からユーザー辞書の登録・編集・削除・TSV取り込み、公開辞書の更新を行えます。利用者がPythonをインストールする必要はありません。
- 通知領域の管理アイコンから設定画面を開けます。Windowsの標準「あ」インジケーターへの統合表示はまだ保証しません。

公開バイナリに向けた残課題は、実アプリ横断のTSF検証、正式な候補UI Automation、文節編集、更新・削除の実機検証、依存バイナリのNOTICE確認です。詳しい実測と制約は[RELEASE.md](RELEASE.md)に記録しています。署名のない個人評価と、一般向けの公開配布は別の判断として扱います。

## 仕組み

```text
キー入力 → TSF TIP（C++） → 即時プレビューと候補窓
                         ↘ 非同期の専用エンジンプロセス
                            ├─ AzooKey 辞書 / Zenzai モデル
                            ├─ 公開辞書
                            └─ ユーザー辞書
```

候補要求には入力世代と文脈の識別子を付け、古い結果が新しい入力や手動選択を上書きしないようにしています。モデル推論と辞書検索はローカルです。公開辞書の更新以外に入力中のネットワーク通信はありません。

## 開発と検証

Windows 11 x64、MSVC、CMakeを使います。Pythonと`uv`は開発用テスト・素材生成にのみ必要です。ネイティブビルドには別途[myime](https://github.com/unok/myime)の固定リビジョン `a8486eca5312556ff88fed7f1850a28843b67977`、その依存ソース、モデル、辞書を用意します。**クリーンPCでの一括再現手順はまだ整備中**です。現時点で以下は依存物を配置済みの開発環境での実行例です。

```powershell
cmake -S native -B native/build -A x64
cmake --build native/build --config Release --target engine_test quality_typo_long release_test ime_mixed_tip ime_engine_host ime_settings ime_tray
native\build\Release\engine_test.exe
native\build\Release\quality_typo_long.exe
native\build\Release\release_test.exe
uv run pytest -q
```

2026-09-26時点のエンジン回帰は `engine_test` 全件成功、長文品質46/46、配布試験105/105、pytest 11/11、Python受入32/32です。0.2.7-previewのTIP実機E2Eは、単一Rich Editホストで32成功・0失敗でした。長文英日混在と `sannkai` を含みます。これらの数字は全アプリ互換性を示すものではありません。

主な場所: `native/src/` にTIP・エンジン・設定画面、`native/tests/` にネイティブ回帰、`native/assets/` に公開辞書、`src/ime_mixed/` に初期段階のPython実験、`scripts/` に辞書更新・検証、[仕様書](windows_modeless_ai_ime_design_v2_google_ux_ja.md)と[RELEASE.md](RELEASE.md)に設計と評価の記録があります。

## ライセンスと出典

このリポジトリ独自のコードは[MIT](LICENSE)です。変換エンジンは[myime](https://github.com/unok/myime)と[AzooKeyKanaKanjiConverter](https://github.com/azooKey/AzooKeyKanaKanjiConverter)を利用し、辞書・モデル・ランタイムにはそれぞれ別の権利条件があります。とくにEDRDG由来の公開辞書データは **CC BY-SA 4.0** です。ソフトウェア本体を一律にMITと表示して、他者のデータまで再ライセンスする意図はありません。

コンポーネント、固定リビジョン、出典と未完の配布確認は[THIRD_PARTY.md](THIRD_PARTY.md)を参照してください。YomitsuguはGoogle、Mozc、AzooKey、myimeの公式製品ではありません。
