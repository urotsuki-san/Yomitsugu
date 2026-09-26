$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskSetup = Join-Path $taskRoot 'dist/installer/Yomitsugu-0.2.10-preview-x64-setup.exe'
$taskInstallDir = Join-Path $env:ProgramFiles 'Yomitsugu/0.2.10-preview'
$taskDll = Join-Path $taskInstallDir 'ime_mixed_tip_v14.dll'
$taskClsid = 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
$taskRunKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run'
$taskAudit = Join-Path $taskRoot 'audit/installer-smoke'
New-Item -ItemType Directory -Path $taskAudit -Force | Out-Null

if (-not (Test-Path -LiteralPath $taskSetup)) { throw 'Installer missing.' }
if (Test-Path -LiteralPath $taskInstallDir) { throw 'Install target already exists.' }
$taskBefore = if (Test-Path -LiteralPath $taskClsid) { (Get-Item -LiteralPath $taskClsid).GetValue('') } else { $null }
if ($taskBefore) { throw "Another Yomitsugu TIP is registered: $taskBefore" }

$taskInstall = Start-Process -FilePath $taskSetup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=$taskAudit/install.log") -Wait -PassThru -WindowStyle Hidden
if ($taskInstall.ExitCode -ne 0) { throw "Installer failed: $($taskInstall.ExitCode)" }
if (-not (Test-Path -LiteralPath $taskDll)) { throw 'Installed TIP DLL missing.' }
$taskRegistered = if (Test-Path -LiteralPath $taskClsid) { (Get-Item -LiteralPath $taskClsid).GetValue('') } else { $null }
if ($taskRegistered -ne $taskDll) { throw "COM registration mismatch: $taskRegistered" }
$taskTray = (Get-ItemProperty -Path $taskRunKey -Name 'YomitsuguTray' -ErrorAction Stop).YomitsuguTray
if ($taskTray -ne ('"' + (Join-Path $taskInstallDir 'ime_tray.exe') + '"')) { throw "Tray startup mismatch: $taskTray" }
$taskUninstaller = Join-Path $taskInstallDir 'unins000.exe'
if (-not (Test-Path -LiteralPath $taskUninstaller)) { throw 'Windows uninstaller missing.' }

# Swiftのビルド元を隠し、インストール先だけで辞書を読み込めることを確かめる。
$taskSwiftBuild = [IO.Path]::GetFullPath((Join-Path $taskRoot 'upstream/myime/src/swift-engine/.build'))
$taskSwiftHidden = [IO.Path]::GetFullPath((Join-Path $taskRoot 'upstream/myime/src/swift-engine/.build-installed-test'))
foreach ($taskPath in @($taskSwiftBuild,$taskSwiftHidden)) {
    if (-not $taskPath.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Swift test path leaves workspace.' }
}
if (Test-Path -LiteralPath $taskSwiftHidden) { throw 'Swift test backup already exists.' }
$taskMoved = Test-Path -LiteralPath $taskSwiftBuild
try {
    if ($taskMoved) { Move-Item -LiteralPath $taskSwiftBuild -Destination $taskSwiftHidden }
    python (Join-Path $PSScriptRoot 'installed_conversion.py') (Join-Path $taskInstallDir 'engine/ime_engine_host.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Installed conversion failed.' }
} finally {
    if ($taskMoved -and (Test-Path -LiteralPath $taskSwiftHidden)) { Move-Item -LiteralPath $taskSwiftHidden -Destination $taskSwiftBuild }
}

$taskUninstall = Start-Process -FilePath $taskUninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=$taskAudit/uninstall.log") -Wait -PassThru -WindowStyle Hidden
if ($taskUninstall.ExitCode -ne 0) { throw "Uninstaller failed: $($taskUninstall.ExitCode)" }
$taskAfter = if (Test-Path -LiteralPath $taskClsid) { (Get-Item -LiteralPath $taskClsid).GetValue('') } else { $null }
if ($taskAfter -eq $taskDll) { throw 'Uninstaller left COM registration behind.' }
if ((Get-ItemProperty -Path $taskRunKey -Name 'YomitsuguTray' -ErrorAction SilentlyContinue).YomitsuguTray) { throw 'Uninstaller left tray startup behind.' }
Write-Output 'Installer smoke passed: files, registration, installed conversion, tray startup, uninstall cleanup.'
