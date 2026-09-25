$ErrorActionPreference = 'Stop'
$taskAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $taskAdmin) { throw 'Run from an Administrator Windows PowerShell.' }
$taskDll = Join-Path $PSScriptRoot 'ime_mixed_tip_v12.dll'
$taskClsidKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
if (Test-Path -LiteralPath $taskClsidKey) {
    $taskRegistered = (Get-Item -LiteralPath $taskClsidKey).GetValue('')
    if ($taskRegistered -ne $taskDll) { throw 'Another version is registered; this uninstaller will not unregister it.' }
}
$taskReg = Start-Process -FilePath "$env:SystemRoot/System32/regsvr32.exe" -ArgumentList '/u','/s',"`"$taskDll`"" -WindowStyle Hidden -Wait -PassThru
if ($taskReg.ExitCode -ne 0) { throw "Unregister failed: $($taskReg.ExitCode)" }
$taskTray = Join-Path $PSScriptRoot 'ime_tray.exe'
$taskRunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Test-Path -LiteralPath $taskRunKey) {
    $taskRunValue = (Get-ItemProperty -Path $taskRunKey -Name 'ImeMixedTray' -ErrorAction SilentlyContinue).ImeMixedTray
    if ($taskRunValue -eq ('"' + $taskTray + '"')) { Remove-ItemProperty -Path $taskRunKey -Name 'ImeMixedTray' }
}
$taskShortcutFile = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Yomitsugu\設定と辞書.lnk'
if (Test-Path -LiteralPath $taskShortcutFile) {
    $taskShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($taskShortcutFile)
    if ($taskShortcut.TargetPath -eq (Join-Path $PSScriptRoot 'ime_settings.exe')) {
        Remove-Item -LiteralPath $taskShortcutFile -Force
        $taskMenu = Split-Path -Parent $taskShortcutFile
        if (-not (Get-ChildItem -LiteralPath $taskMenu -Force)) { Remove-Item -LiteralPath $taskMenu }
    }
}
Write-Output 'Unregistered. User dictionary retained. Sign out before manually removing this version directory; running applications are never terminated.'
