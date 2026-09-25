$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskPackage = Join-Path $taskRoot 'dist/yomitsugu-0.2.8-preview'
$taskManifest = Get-Content -LiteralPath (Join-Path $taskPackage 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskManifest.version -ne '0.2.8-preview' -or $taskManifest.architecture -ne 'x64') { throw 'Manifest version or architecture mismatch' }
if ((Get-FileHash -LiteralPath (Join-Path $taskRoot 'RELEASE.md') -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $taskPackage 'README.md') -Algorithm SHA256).Hash) {
    throw 'Packaged README differs from RELEASE.md'
}
$taskCritical = @('ime_mixed_tip_v12.dll','ime_dictionary_tool.exe','ime_settings.exe','ime_tray.exe','install_preview.ps1','uninstall_preview.ps1','THIRD_PARTY.md','LICENSE',
  'update_public_dictionary.ps1','PublicDictionaryBuilder.cs','licenses/public-dictionary-NOTICE',
  'engine/ime_engine_host.exe','engine/public_dictionary.tsv','engine/public_dictionary.sources.json',
  'engine/AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary.bundle')
foreach ($taskName in $taskCritical) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskPackage $taskName))) { throw "Critical package asset missing: $taskName" }
}
$taskDictionaryMetadata = Get-Content -LiteralPath (Join-Path $taskPackage 'engine/public_dictionary.sources.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskDictionaryMetadata.format_version -ne 2 -or
    (Get-FileHash -LiteralPath (Join-Path $taskPackage 'engine/public_dictionary.tsv') -Algorithm SHA256).Hash -ne $taskDictionaryMetadata.output_sha256) {
    throw 'Public dictionary provenance hash mismatch.'
}
$taskBytes = 0L
foreach ($taskEntry in $taskManifest.files) {
    $taskFile = [IO.Path]::GetFullPath((Join-Path $taskPackage $taskEntry.path))
    if (-not $taskFile.StartsWith($taskPackage + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid manifest path' }
    if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash -ne $taskEntry.sha256) { throw "Hash mismatch: $($taskEntry.path)" }
    $taskBytes += $taskEntry.bytes
}
$taskActualFiles = @(Get-ChildItem -LiteralPath $taskPackage -File -Recurse)
if ($taskActualFiles.Count -ne $taskManifest.files.Count + 1) { throw 'Manifest file inventory does not match package' }
$taskScripts = @('native/scripts/install_preview.ps1','native/scripts/uninstall_preview.ps1','native/scripts/e2e_run.ps1','scripts/package_release.ps1','scripts/archive_release.ps1','scripts/check_release.ps1')
foreach ($taskScript in $taskScripts) {
    $taskTokens = $null
    $taskErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $taskRoot $taskScript),[ref]$taskTokens,[ref]$taskErrors) | Out-Null
    if ($taskErrors.Count) { throw "PowerShell syntax error in $taskScript : $taskErrors" }
}
$taskSignature = (Get-AuthenticodeSignature -LiteralPath (Join-Path $taskPackage 'ime_mixed_tip_v12.dll')).Status.ToString()
$taskReport = [ordered]@{files_verified=$taskManifest.files.Count; bytes=$taskBytes; tip_signature=$taskSignature; public_release_ready=$taskManifest.public_release_ready; powershell_parse='passed'}
$taskAuditDir = Join-Path $taskRoot 'audit/2026-09-25/release'
New-Item -ItemType Directory -Path $taskAuditDir -Force | Out-Null
$taskReport | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskAuditDir 'package_validation.json') -Encoding UTF8
$taskReport | ConvertTo-Json -Compress
