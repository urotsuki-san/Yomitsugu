$ErrorActionPreference = 'Stop'
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:GITHUB_REPOSITORY -ne 'urotsuki-san/Yomitsugu') {
    throw 'この試験は GitHub の使い捨て Windows ランナーで実行する。'
}
$result = Join-Path $PWD 'audit/chrome'
New-Item -ItemType Directory -Path $result -Force | Out-Null
$installer = Join-Path $PWD 'audit/ci-installer/Yomitsugu-0.2.10-preview-x64-setup.exe'
$installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
$install = Start-Process $installer -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -PassThru -Wait
if ($install.ExitCode -ne 0) { throw "Setup exit $($install.ExitCode)" }
$registration = Get-Item 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InprocServer32'
$dll = $registration.GetValue('')
$root = Join-Path $env:ProgramFiles 'Yomitsugu/0.2.10-hybrid.14'
if ($dll -ne (Join-Path $root 'ime_mixed_tip_v14.dll')) { throw "Unexpected TIP: $dll" }
$tip = '0411:{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5C}'
$languages = New-WinUserLanguageList ja-JP
$languages[0].InputMethodTips.Clear()
$languages[0].InputMethodTips.Add($tip)
$languages.Add('en-US')
Set-WinUserLanguageList $languages -Force
Set-WinDefaultInputMethodOverride -InputTip $tip
Get-WinUserLanguageList | Out-String | Write-Host
Get-Process -Id $PID | Select-Object ProcessName,SessionId
Get-Process -Name explorer,ctfmon -ErrorAction SilentlyContinue | Select-Object ProcessName,SessionId
$hash=(Get-FileHash $dll).Hash
$chromePath=@($env:ProgramFiles, ${env:ProgramFiles(x86)}) | ForEach-Object { Join-Path $_ 'Google/Chrome/Application/chrome.exe' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$worker=Join-Path $root 'engine/ime_engine_host.exe'
@{source=$env:GITHUB_SHA; image=$env:ImageVersion; os=[Environment]::OSVersion.Version.ToString()
  chrome=(Get-Item $chromePath).VersionInfo.ProductVersion; installer_sha256=$installerHash; tip_sha256=$hash
  worker_sha256=(Get-FileHash $worker).Hash
  settings_sha256=(Get-FileHash (Join-Path $root 'ime_settings.exe')).Hash
  models=@(Get-ChildItem (Join-Path $root 'engine/models') -File -Recurse | Select-Object Name,Length)
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $result 'environment.json') -Encoding utf8
python -I -B -X utf8 scripts/ci/chrome/chrome_ime.py --native --expected-tip-sha256 $hash --selector build/Release/chrome_select_profile.exe --result "$result/native.json"
exit $LASTEXITCODE
