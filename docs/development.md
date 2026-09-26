# 開発ガイド

Windows x64、CMake、MSVC、Swift 6.3.3を使います。配布物の生成にはInno Setup 6.7.1が必要です。依存先のコミットとモデルのハッシュは `scripts/ci/prepare_runtime.ps1` に固定しています。

## ビルド

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ci/prepare_runtime.ps1
cmake -S native -B native/build -A x64
cmake --build native/build --config Release --target app_update_test engine_test debug_dump quality_typo_long release_test ime_mixed_tip ime_engine_host ime_dictionary_tool ime_settings ime_tray ime_profile_control e2e_tip_tests tip_com_smoke
powershell -NoProfile -File scripts/stage_engine.ps1 -Destination native/build/e2e_out_v13/engine
powershell -NoProfile -File scripts/ci/stage_test_runtime.ps1
```

Swiftが生成する `.resources` または `.bundle` の名前は変えずに配置します。変換エンジンは実行ファイルの隣から辞書とモデルを読み込みます。

## 回帰試験

```powershell
native/build/Release/app_update_test.exe
native/build/Release/engine_test.exe
native/build/Release/quality_typo_long.exe
native/build/Release/release_test.exe
uv run pytest -q
uv run python -c "from ime_mixed.evaluate import evaluate_seed_file; r=evaluate_seed_file('ime_acceptance_seed_cases.jsonl'); print(r['summary']); assert r['summary']['failed']==0"
python scripts/evaluate_conversion.py --output audit/conversion-quality.json --check
```

更新機能の通信とファイル検証は、次のコマンドで試せます。取得したインストーラーは実行しません。

```powershell
native/build/Release/app_update_test.exe --live audit/update-download
```

実インストール後は `scripts/ci/installed_conversion.py` にインストール先の `engine/ime_engine_host.exe` を渡します。試験用の辞書と学習履歴は一時フォルダーへ分けます。CIではSwiftのビルド元を一時退避し、インストール先だけで変換できることも確認します。

## 画面での入力試験

`e2e_tip_tests.exe` はRich Editの入力欄を開いてキーを送ります。`--reported-inputs` を付けると、インストール・アンインストールなどを入力し、候補を待つ場合と入力直後にEnterを押す場合を比較します。

この試験は前面の入力欄を操作します。作業を保存し、操作を中断できるときに実行してください。通常の `ctest` には含めていません。

## パッケージとインストーラー

```powershell
powershell -NoProfile -File scripts/package_release.ps1
powershell -NoProfile -File scripts/validate_package.ps1
powershell -NoProfile -File scripts/archive_release.ps1
python scripts/verify_archive.py
powershell -NoProfile -File scripts/build_installer.ps1
```

成果物は `dist/installer` に出力されます。GitHub Actionsでも同じ手順で生成し、インストール・変換・削除の試験後に成果物を保存します。

使用中のDLLを上書きしないよう、版ごとにDLL名とインストール先を分けています。バージョンを上げる際はCMake、リソース情報、パッケージ用スクリプト、Inno Setup、更新機能の `kReleaseTag` を合わせて変更します。
