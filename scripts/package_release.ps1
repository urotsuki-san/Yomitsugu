$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskPackage = Join-Path $taskRoot 'dist/yomitsugu-0.2.9-preview'
if (Test-Path -LiteralPath $taskPackage) { throw 'Package directory already exists; use a new version instead of overwriting.' }
New-Item -ItemType Directory -Path $taskPackage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/e2e_out_v13/ime_mixed_tip_v13.dll') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/e2e_out_v13/engine') -Destination $taskPackage -Recurse
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/Release/ime_dictionary_tool.exe') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/Release/ime_settings.exe') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/Release/ime_tray.exe') -Destination $taskPackage
foreach ($taskPattern in @('msvcp140*.dll','vcruntime140*.dll','concrt140.dll')) {
    Get-ChildItem -LiteralPath (Join-Path $taskRoot 'upstream/myime/build/x64/release') -Filter $taskPattern -File | Copy-Item -Destination $taskPackage
}
foreach ($taskScript in @('install_preview.ps1','uninstall_preview.ps1')) { Copy-Item -LiteralPath (Join-Path $taskRoot "native/scripts/$taskScript") -Destination $taskPackage }
Copy-Item -LiteralPath (Join-Path $taskRoot 'scripts/update_public_dictionary.ps1') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskRoot 'scripts/PublicDictionaryBuilder.cs') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskRoot 'RELEASE.md') -Destination (Join-Path $taskPackage 'README.md')
Copy-Item -LiteralPath (Join-Path $taskRoot 'THIRD_PARTY.md') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskRoot 'LICENSE') -Destination $taskPackage
$taskNotices = Join-Path $taskPackage 'licenses'
New-Item -ItemType Directory -Path $taskNotices | Out-Null
$taskLicenses = @{
    'myime-LICENSE'='upstream/myime/LICENSE';
    'azookey-converter-LICENSE'='upstream/myime/src/AzooKeyKanaKanjiConverter/LICENSE';
    'azookey-dictionary-LICENSE'='upstream/myime/src/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_dictionary_storage/LICENSE';
    'zenzai-model-LICENSE'='upstream/myime/src/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_dictionary_storage/LICENSE';
    'llama-cpp-LICENSE'='upstream/myime/llama.cpp-src/LICENSE';
    'swift-LICENSE'='native/third_party/swift-LICENSE.txt';
    'unicode-LICENSE'='native/third_party/unicode-LICENSE.txt';
    'emoji-dictionary-README'='upstream/myime/src/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_emoji_dictionary_storage/README.md';
    'emoji-dictionary-DATA-README'='upstream/myime/src/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_emoji_dictionary_storage/data/README.md';
    'nlohmann-json-LICENSE'='native/third_party/nlohmann/LICENSE.MIT'
    'public-dictionary-NOTICE'='native/third_party/public-dictionary-NOTICE.txt'
}
foreach ($taskName in $taskLicenses.Keys) { Copy-Item -LiteralPath (Join-Path $taskRoot $taskLicenses[$taskName]) -Destination (Join-Path $taskNotices $taskName) }
$taskManifest = [ordered]@{
    version='0.2.9-preview'; architecture='x64'; public_release_ready=$false;
    created_utc=(Get-Date).ToUniversalTime().ToString('o');
    upstream_myime='a8486eca5312556ff88fed7f1850a28843b67977';
    dictionary='4d418525b090cf49c219819d05a7e3cc2a4346eb';
    release_gates=@('publisher_signature','binary_model_license_review','TSF_E2E_and_cross_app_matrix','installer_and_upgrade_validation','IME_feature_completeness');
    files=@()
}
foreach ($taskFile in (Get-ChildItem -LiteralPath $taskPackage -Recurse -File | Sort-Object FullName)) {
    $taskManifest.files += [ordered]@{
        path=$taskFile.FullName.Substring($taskPackage.Length+1).Replace('\','/');
        bytes=$taskFile.Length; sha256=(Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash
    }
}
$taskManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskPackage 'manifest.json') -Encoding UTF8
Write-Output "Preview package: $taskPackage"
Write-Output "Files: $($taskManifest.files.Count); public_release_ready=False"
