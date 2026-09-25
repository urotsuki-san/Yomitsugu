$ErrorActionPreference = 'Stop'
$taskKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
$taskRegistered = if (Test-Path -LiteralPath $taskKey) { (Get-Item -LiteralPath $taskKey).GetValue('') } else { '(none)' }
Write-Output "registered=$taskRegistered"
$taskRoot = Join-Path $env:ProgramFiles 'ImeMixed'
if (Test-Path -LiteralPath $taskRoot) { Get-ChildItem -LiteralPath $taskRoot -Directory | Select-Object -ExpandProperty Name }
$taskRun = Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'ImeMixedTray' -ErrorAction SilentlyContinue
Write-Output "tray=$($taskRun.ImeMixedTray)"
