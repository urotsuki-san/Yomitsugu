$ErrorActionPreference = 'Stop'
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:GITHUB_REPOSITORY -ne 'urotsuki-san/Yomitsugu') {
    throw 'この試験は GitHub の使い捨て Windows ランナーで実行する。'
}
$result = Join-Path $PWD 'audit/chrome'
New-Item -ItemType Directory -Path $result -Force | Out-Null
$installer = Join-Path $PWD 'audit/ci-installer/Yomitsugu-0.2.10-preview-x64-setup.exe'
$installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
function Install-PackageForTest([string]$path) {
    $setup = Start-Process $path -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -PassThru -Wait
    if ($setup.ExitCode -ne 0) { throw "Setup exit $($setup.ExitCode)" }
}
$oldInstaller = Join-Path $env:RUNNER_TEMP 'previous-yomitsugu.exe'
Invoke-WebRequest 'https://github.com/urotsuki-san/Yomitsugu/releases/download/v0.2.10-preview.2/Yomitsugu-0.2.10-preview-x64-setup.exe' -OutFile $oldInstaller
if ((Get-FileHash $oldInstaller).Hash -ne '86FA037551465AAB6DFE4BB98F847A1620ACF474F26E19F5E5711083083618BA') { throw 'Previous installer hash mismatch' }
Install-PackageForTest $oldInstaller
$registrationKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InprocServer32'
$oldDll = (Get-Item $registrationKey).GetValue('')
if ($oldDll -ne (Join-Path $env:ProgramFiles 'Yomitsugu/0.2.10-hybrid.12/ime_mixed_tip_v14.dll')) { throw "Unexpected previous TIP: $oldDll" }
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
$chromePath=@($env:ProgramFiles, ${env:ProgramFiles(x86)}) | ForEach-Object { Join-Path $_ 'Google/Chrome/Application/chrome.exe' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$oldProfile = Join-Path $env:RUNNER_TEMP 'chrome-before-ime-update'
$page = [Uri](Resolve-Path (Join-Path $PSScriptRoot 'fields.html')).Path
$browser = Start-Process $chromePath -ArgumentList @("--user-data-dir=`"$oldProfile`"", '--no-first-run', '--no-default-browser-check', '--disable-background-mode', '--start-maximized', $page.AbsoluteUri) -PassThru -WindowStyle Normal
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        & build/Release/chrome_select_profile.exe $browser.Id
        $selected = $LASTEXITCODE -eq 0
        $modules = (Get-Process -Id $browser.Id).Modules.FileName
    } until (($selected -and $modules -contains $oldDll) -or [DateTime]::UtcNow -gt $deadline)
    if (!$selected -or $modules -notcontains $oldDll) { throw 'Old Chrome session did not load the previous TIP' }

    Install-PackageForTest $installer
    $dll = (Get-Item $registrationKey).GetValue('')
    $root = Join-Path $env:ProgramFiles 'Yomitsugu/0.2.10-hybrid.14'
    if ($dll -ne (Join-Path $root 'ime_mixed_tip_v14.dll')) { throw "Unexpected upgraded TIP: $dll" }
    $modules = (Get-Process -Id $browser.Id).Modules.FileName
    @{before=$oldDll; registered_after=$dll; old_still_loaded=($modules -contains $oldDll)
      new_already_loaded=($modules -contains $dll)} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $result 'open-during-update.json') -Encoding utf8
} finally {
    $browser.Refresh()
    if (!$browser.HasExited) {
        [void]$browser.CloseMainWindow()
        if (!$browser.WaitForExit(10000)) { throw 'Previous Chrome process did not exit' }
    }
}
$hash=(Get-FileHash $dll).Hash
$worker=Join-Path $root 'engine/ime_engine_host.exe'
@{source=$env:GITHUB_SHA; image=$env:ImageVersion; os=[Environment]::OSVersion.Version.ToString()
  chrome=(Get-Item $chromePath).VersionInfo.ProductVersion; installer_sha256=$installerHash; tip_sha256=$hash
  worker_sha256=(Get-FileHash $worker).Hash
  settings_sha256=(Get-FileHash (Join-Path $root 'ime_settings.exe')).Hash
  models=@(Get-ChildItem (Join-Path $root 'engine/models') -File -Recurse | Select-Object Name,Length)
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $result 'environment.json') -Encoding utf8
python -I -B -X utf8 scripts/ci/chrome/chrome_ime.py --native --expected-tip-sha256 $hash --selector build/Release/chrome_select_profile.exe --result "$result/native.json"
exit $LASTEXITCODE
