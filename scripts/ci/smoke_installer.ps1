$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskSetup = Join-Path $taskRoot 'dist/installer/Yomitsugu-0.2.7-preview-x64-setup.exe'
$taskInstallDir = Join-Path $env:ProgramFiles 'Yomitsugu/0.2.7-preview'
$taskDll = Join-Path $taskInstallDir 'ime_mixed_tip_v11.dll'
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

$taskUninstall = Start-Process -FilePath $taskUninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=$taskAudit/uninstall.log") -Wait -PassThru -WindowStyle Hidden
if ($taskUninstall.ExitCode -ne 0) { throw "Uninstaller failed: $($taskUninstall.ExitCode)" }
$taskAfter = if (Test-Path -LiteralPath $taskClsid) { (Get-Item -LiteralPath $taskClsid).GetValue('') } else { $null }
if ($taskAfter -eq $taskDll) { throw 'Uninstaller left COM registration behind.' }
if ((Get-ItemProperty -Path $taskRunKey -Name 'YomitsuguTray' -ErrorAction SilentlyContinue).YomitsuguTray) { throw 'Uninstaller left tray startup behind.' }
Write-Output 'Installer smoke passed: files, COM registration, tray startup, uninstall cleanup.'
