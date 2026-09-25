# Yomitsugu 0.2.8 Preview

ローマ字を続けて打ち、入力中に日本語の変換候補を提示する Windows IME です。候補の選択・確定は利用者が行います。

2026-09-26時点の開発中の評価版です。0.2.8では長音での誤分割、ローマ字表の欠落、辞書に基づくタイポ候補を修正しました。未署名のWindows x64版です。

## 0.2.8の変更

- 長音の途中で語を分けて誤った候補を優先する問題を修正。
- Mozcのローマ字表323行を出典付きで採用し、`we`、拡張音、子音の持ち越しを補完。
- `aninsuto-ru`、`anninsuto-ru` などの撥音の曖昧さを辞書で照合。隣接打鍵の入れ替え、重複、抜け、置換、長音位置のずれも候補として提示。
- 公開辞書を記号・略語から一般名詞・外来語へ拡張。文字化けを避けるため公式UTF-8版へ移行。
- 辞書由来の英字保持用索引を追加。`software`などの英語表記と、後続の日本語入力を両立。
- 追加85例で外来語31/31が先頭、タイポ23例中20例が5候補以内。すべての入力を正しく補正できるわけではありません。

## 基本機能

- `a`／`i`／`u`／`e`／`o` は日本語の文章欄で「あ」／「い」／「う」／「え」／「お」を第一候補にし、英字も候補に残す。識別子欄では英字を維持する。
- 辞書管理用のアイコンを通知領域へ登録し、右クリックから設定・ユーザー辞書の登録／編集・TSV取り込み・公開辞書の更新・説明へ進める。Windowsがアイコンを「隠れているインジケーター」に収める場合は `^` の一覧から開く。スタートメニューの「Yomitsugu → 設定と辞書」と `ime_settings.exe` からも開ける。
- `ri-domi-wokousinnsitekudasaiGithubde` は公開辞書を文頭語に使い、後続の日本語と英語を分割して `READMEを更新してくださいGithubで` を提示する。
- `sannkai` の読み「さんかい」では、数字・助数詞の省略表記より辞書の一般語を優先し、「散開」を先頭候補にする。`toritatigasannkaisuru` の「鳥たちが散開する」は候補に残りますが、文脈に合った同音語を先頭に選ぶ改善は残っています。
- ユーザー辞書の登録画面を追加。読み・単語・品詞の登録、更新、削除、保存をアプリ内で行う。辞書フォルダーがなくても開け、保存時に作成する。保存前に形式と件数を検査し、旧ファイルを `.bak` に退避する。
- 公開辞書の更新はWindows標準のPowerShell/.NETで実行。利用者によるPythonの導入は不要。公式のUTF-8データを使い、開発用生成器との出力SHA-256一致を検証しています。
- 0.2.5ではTSF項目の登録成功を実画面の表示成功と誤認していた。Windowsが互換性条件を満たさないIMEの統合モードアイコンを表示しない場合があるため、このPreviewでは統合モードアイコンの可視性を保証しない。別の辞書管理アイコンで操作経路を提供する。
- `ltu`／`xtu`／`ltsu`／`xtsu` を小さい「っ」に変換し、`samukunaltutekimasitane` の最優先候補を「寒くなってきましたね」に修正。`saikilyou` も「最強」を最優先にする。
- [Mozcの記号辞書](https://github.com/google/mozc/blob/master/src/data/symbol/symbol.tsv)と[EDRDGのUTF-8 EDICT2辞書](https://www.edrdg.org/pub/Nihongo/edict2u.gz)から公開辞書を生成。176,481かな読み・277,889変換候補（一般語・外来語274,024、記号3,115、英語略語750）と英字保持用21,632項目、計299,521項目。本辞書や異表記との重複を含むため、そのまま語彙数とは数えません。
- 記号読みと一般語が重なる場合は一般語の先頭候補を保持し、記号を後続候補に置く。矢印は記号を先頭に置く。個人辞書の登録語はこれらより優先する。
- 文章中の半角ピリオド／カンマは全角の「。」「、」を優先し、原文候補も残す。
- AzooKeyの辞書候補を日本語変換の主候補に使用。手書き辞書は補正候補・障害時の補助に変更。
- 同梱辞書を実測：422シャード、297,755の空でない読みノード、566,214辞書レコード。活用形・重複を含むため「56万語」とは表記しない。
- Swift・Zenzai・辞書は `engine/ime_engine_host.exe` 内で実行。入力先アプリにSwift DLLを読み込ませず、`.bundle` を入力先EXEの隣に配布する必要をなくした。
- 非同期の候補更新。未処理要求は最新1件、実行中は1件。古い世代・手動選択後の結果を破棄。エンジン停止／起動失敗時も入力を保持。
- カーソル位置での挿入・Backspace・Delete、変換中の取消、読みを保持したF6～F10。512バイト超は探索を止めて原文を保持する。
- TSF編集結果を確認してからセッション状態を反映。編集要求は文脈・処理内容を所有し、非同期処理で共有の一時変数を使用しない。
- 候補窓のキャレット位置、宿主所有ウィンドウ、モニタ作業領域、DPI、Windows標準色／フォント、クリック選択、標準リストのアクセシビリティ。
- Ctrl／Alt／Windowsのショートカットを奪わず、文字入力は現在のキーボード配列から取得。入力禁止・読み取り専用・パスワード／PINの入力スコープを確認。
- リリースでキーコード・入力文字をファイルやデバッグ出力に記録しない。学習は既定でオフ。辞書検索とモデル推論はローカルで行う。

## 評価環境

Windows 11、x64デスクトップ、CMake＋MSVC。x86、ARM64、AppContainerの対応は宣言しません。UIElement／Immersive対応カテゴリの虚偽登録を取り除きました。

## ビルドと検証

```powershell
cmake -S native -B native/build -A x64
cmake --build native/build --config Release --target engine_test quality_typo_long release_test ime_mixed_tip ime_engine_host ime_dictionary_tool ime_settings ime_tray ime_profile_control e2e_tip_tests
powershell -NoProfile -File scripts/check_release.ps1 -Mode native
powershell -NoProfile -File scripts/check_release.ps1 -Mode release
powershell -NoProfile -File scripts/check_release.ps1 -Mode python
```

`release_test` は実辞書がなければ失敗します。0.2.8で `engine_test` ALL PASSED、品質46/46、`release_test` 146/146、pytest19/19、Python受入32/32を確認しました。追加85例の結果と未解決例は [変換品質の記録](https://github.com/urotsuki-san/Yomitsugu/blob/main/docs/conversion-quality.md)にあります。利用者のクリーンPCでの実動作は未検証です。

デスクトップを操作するE2Eは通常の `ctest` に含めません。今回の0.2.8では実行していません。旧0.2.7（v11）のRich Edit単一ホストでの32成功・0失敗を、0.2.8の実アプリ検証としては扱いません。0.2.8用の出力先は `native/build/e2e_out_v12/` です。

## ユーザー辞書

UTF-8（BOMあり／なし）のTSV。Google日本語入力／Mozc形式の「よみ・単語・品詞」を読みます。読み全体が一致する登録語は優先候補にします。文中では最初に見つかる最長の登録語について、前後の読みを保持した候補を追加します。1文字のかなの部分一致は対象外で、同時に複数の登録語を組み合わせる解析は未対応です。

1,000件以下ではAzooKeyの動的辞書APIにも渡し、一般名詞・固有名詞・人名・地名などを分類へ対応付けます。ただしAPIが成功しても文中候補へ現れないケースを確認したため、登録語の提示はC++側でも保証する設計です。1,000件を超える場合はC++側の索引検索を使い、上流の線形検索による遅延を避けます。活用生成や全品詞の互換性は未検証です。

```text
こでっくす	Codex	名詞
しゃないつーる	社内ツール	名詞
```

```powershell
.\ime_dictionary_tool.exe --import "C:\path\dictionary.tsv"
```

保存先は `%LOCALAPPDATA%\ImeMixed\user_dictionary.tsv`。設定画面の「ユーザー辞書を編集」から登録・更新・削除・保存できます。保存前の変更は「閉じる」で確認します。TSVインポートは**既存辞書の置換**です。前の内容を `.bak` に保存し、不正なファイルは反映しません。最大4MiB、10万件、同一読み32候補。更新は次回の変換要求で読み直します。バックアップを再インポートすれば戻せます。個人辞書は配布物に含めません。

## 配布フォルダとインストール

`scripts/package_release.ps1` で `dist/yomitsugu-0.2.8-preview` を生成します。DLL／実行ファイル、専用エンジン、辞書、モデル、依存ライセンス、SHA-256一覧をまとめます。ZIPの横に同名の `.zip.sha256` を置きます。パッケージ生成は既存フォルダを上書きせず失敗します。`scripts/build_installer.ps1` は検証済みパッケージから Inno Setup の `Yomitsugu-0.2.8-preview-x64-setup.exe` を作ります。GitHub Actions は `main` 更新時と手動実行時に同じ工程を実行し、インストール・削除のスモーク試験後に `yomitsugu-0.2.8-preview-installer` を7日間保存します。

利用者向けの評価版は [GitHub Releases](https://github.com/urotsuki-san/Yomitsugu/releases)からダウンロードできます。インストーラーのSHA-256も同じ画面に記載します。`public_release_ready=false` は、安定版としての一般配布条件が未達という意味です。

評価用インストーラーは管理者権限で起動し、64bit Windows にインストールします。DLLの登録、スタートメニュー項目、通知領域アイコンのログイン起動を設定し、Windows の「インストールされているアプリ」からアンインストールできます。既定IMEと個人辞書は変更しません。旧PowerShell版を使用中なら先に旧版の `uninstall_preview.ps1` で登録解除し、サインアウトしてからインストーラーを実行してください。未署名なので Windows が警告を表示する場合があります。

以下は ZIP 版を手動で登録する開発用手順です。こちらの通常インストールスクリプトは公開判定と署名を検証します。本版は `public_release_ready=false` なので、開発評価を行う場合だけ、**管理者として開いた64bit PowerShell**で配布フォルダに移動し、次を実行してください。PowerShellではカレントディレクトリのスクリプトに `./` または `.\` を付ける必要があります。

```powershell
.\install_preview.ps1 -DeveloperUnsigned
```

Program Filesのバージョン別フォルダにコピーし、既定IMEは変更しません。既存版の登録がある場合は、失敗時にその登録へ戻す処理があります。

削除はインストール先フォルダで `.\uninstall_preview.ps1` を管理者PowerShellから実行します。登録解除と同時にこの版のログイン起動設定・ショートカットを削除し、ユーザー辞書は維持します。実行中アプリは終了しません。ファイル削除はサインアウト後に行います。既定の日本語IMEは変更しません。

## クラウド辞書の方針

入力文をサーバーへ送らず、上記2つの固定URLから辞書ファイルだけをHTTPSで取得します。同梱辞書でオフライン利用できます。通知領域アイコンの右クリックか設定画面から「公開辞書を更新」を選んでください。Windows標準のPowerShell/.NETを内部で使います。出力は `%LOCALAPPDATA%\ImeMixed\public_dictionary.tsv`、個人辞書とは別です。更新器は圧縮前後のサイズ、収録件数、必須候補を検証して置換し、出典とSHA-256を隣の `.sources.json` に保存します。旧形式のキャッシュが残っている場合は新しい同梱辞書を使います。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\update_public_dictionary.ps1
```

0.2.6の設定画面からの更新操作を実測し、個人用公開辞書キャッシュの更新と出力SHA-256の一致を確認しました。手動更新時だけ固定URLへGETを送ります。入力中のネットワーク呼び出しと自動更新はありません。HTTPSと形式検証を利用しますが、辞書提供元の署名検証や固定ハッシュによる承認は未実装です。公開辞書の派生データにはEDRDGのCC BY-SA 4.0が適用されます。出典とライセンスは `licenses/public-dictionary-NOTICE` を参照してください。

## 個人利用で残る検証・機能

1. Chrome／Edge、Office、VS Code等での互換性と実入力の継続試験。現行のE2EはRich Editの単一ホストで実行する。
2. 日本語の文節別再変換・文節伸縮・再変換／Undoなど標準IMEとしての機能完成。
3. UI Automationの正式な候補UI契約、スクリーンリーダー、複数DPI、タッチでの実測。
4. パスワード・PIN・入力禁止の実アプリ検証、TSFの編集拒否／フォーカス競合の注入試験、結合文字／絵文字クラスタの編集試験。
5. 複数アプリでのエンジン常駐メモリと起動時間、共有サービス化の評価。現在はTIPインスタンス単位の子プロセス。
6. 別ユーザー／利用者のクリーン環境へのインストール、設定画面の視覚・スクリーンリーダー評価。CI上でのビルド、インストール、削除のスモーク試験は通過済み。

正式版に向けて、依存バイナリ／モデルのNOTICE確認と上記の互換性・機能検証を継続します。今回の評価版にコード署名はありません。

各項目の根拠・実測値は `audit/2026-09-25/release/` と修正報告を参照してください。旧38件の監査報告を「全件解決」とは扱いません。

## 参照した一次資料

- [Microsoft: IME要件](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements)：候補窓の所有、DPI、アクセシビリティ、登録要件を設計の基準に使用。
- [Microsoft: EM_GETEDITSTYLE](https://learn.microsoft.com/en-us/windows/win32/controls/em-geteditstyle)・[EM_SETEDITSTYLE](https://learn.microsoft.com/en-us/windows/win32/controls/em-seteditstyle)：Rich Editの `SES_USECTF` 既定値とTSF有効化を確認。E2Eホストの設定漏れを修正した根拠。
- [Mozc: MS-IME keymap](https://github.com/google/mozc/blob/master/src/data/keymap/ms-ime.tsv)：状態別の取消とF6～F10を参照。
- [Microsoft: ITfInputScope](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nn-inputscope-itfinputscope)：アプリが指定する入力欄の種類を取得。
- [Microsoft: Language Bar (Text Services)](https://learn.microsoft.com/en-us/windows/win32/tsf/language-bar)：入力インジケーターの項目とメニューの登録・削除・通知インターフェースを確認。
- [AzooKey: Zenzai](https://github.com/azooKey/AzooKeyKanaKanjiConverter/blob/main/Docs/zenzai.md)：CPU推論の設定と文脈対応の範囲を確認。現在のブリッジはZenzaiの前後文脈APIまで拡張していない。

一次資料の要件を参照したことと、製品が全要件へ適合したことは別です。未検証項目は上記の配布条件に残しています。
