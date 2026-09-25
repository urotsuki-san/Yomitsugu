param([switch]$Elevated)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell is required.' }
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskAudit = Join-Path $taskRoot 'audit\2026-09-25\release'
$taskStatus = Join-Path $taskAudit 'upgrade_0.2.7_status.json'
$taskPackage = Join-Path $taskRoot 'dist\yomitsugu-0.2.7-preview'
$taskOldDir = Join-Path $env:ProgramFiles 'ImeMixed\0.2.6-preview'
$taskNewDir = Join-Path $env:ProgramFiles 'ImeMixed\0.2.7-preview'
$taskOldDll = Join-Path $taskOldDir 'ime_mixed_tip_v10.dll'
$taskNewDll = Join-Path $taskNewDir 'ime_mixed_tip_v11.dll'
$taskKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
$taskIsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
function Get-RegisteredDll {
  if (Test-Path -LiteralPath $taskKey) { return (Get-Item -LiteralPath $taskKey).GetValue('') }
  return ''
}
function Save-Status([string]$state,[string]$detail) {
  [ordered]@{version='0.2.7-preview';state=$state;detail=$detail;registered_dll=(Get-RegisteredDll);timestamp_utc=(Get-Date).ToUniversalTime().ToString('o')} |
    ConvertTo-Json | Set-Content -LiteralPath $taskStatus -Encoding UTF8
}
New-Item -ItemType Directory -Path $taskAudit -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $taskPackage 'manifest.json'))) { throw 'Validated package missing.' }
if (-not (Test-Path -LiteralPath $taskOldDll)) { throw 'Installed 0.2.6 TIP missing.' }
if (Test-Path -LiteralPath $taskNewDir) { throw '0.2.7 already installed; refusing to overwrite loaded files.' }
$taskRegistered = Get-RegisteredDll
if ($taskRegistered -and -not [string]::Equals($taskRegistered,$taskOldDll,[StringComparison]::OrdinalIgnoreCase)) {
  throw "Unexpected TIP registration: $taskRegistered"
}
if (-not $taskIsAdmin) {
  if ($Elevated) { throw 'Elevation failed.' }
  Save-Status 'awaiting_elevation' 'Old installation checked.'
  $taskPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $taskProcess = Start-Process -FilePath $taskPowerShell -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"' + $PSCommandPath + '"'),'-Elevated')
  $taskHandle = $taskProcess.Handle
  $taskProcess.WaitForExit()
  $taskProcess.Refresh()
  $taskResult = Get-Content -LiteralPath $taskStatus -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($taskProcess.ExitCode -eq 0 -and $taskResult.state -eq 'installed') {
    Get-CimInstance Win32_Process -Filter "Name='ime_tray.exe'" | Where-Object {
      $_.ExecutablePath -and [string]::Equals($_.ExecutablePath,(Join-Path $taskOldDir 'ime_tray.exe'),[StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
    Start-Process -FilePath (Join-Path $taskNewDir 'ime_tray.exe') -WindowStyle Hidden | Out-Null
  }
  Write-Output "upgrade state=$($taskResult.state) registered=$($taskResult.registered_dll)"
  exit $taskProcess.ExitCode
}
try {
  Save-Status 'uninstalling_old' 'Removing 0.2.6 registration and startup.'
  & (Join-Path $taskOldDir 'uninstall_preview.ps1') *> (Join-Path $taskAudit 'upgrade_uninstall_0.2.6.log')
  if (Get-RegisteredDll) { throw 'Old TIP registration remains after uninstall.' }
  Save-Status 'installing_new' 'Copying and registering 0.2.7.'
  & (Join-Path $taskPackage 'install_preview.ps1') -DeveloperUnsigned *> (Join-Path $taskAudit 'upgrade_install_0.2.7.log')
  if (-not [string]::Equals((Get-RegisteredDll),$taskNewDll,[StringComparison]::OrdinalIgnoreCase)) { throw '0.2.7 registration mismatch.' }
  & (Join-Path $taskRoot 'native\build\Release\ime_profile_control.exe') --check-google *> (Join-Path $taskAudit 'upgrade_profile_0.2.7.log')
  if ($LASTEXITCODE -ne 0) { throw 'Default Japanese IME changed unexpectedly.' }
  Save-Status 'installed' '0.2.7 registered; Google remains default.'
  exit 0
} catch {
  $taskReason = $_.Exception.Message
  $taskCurrent = Get-RegisteredDll
  if (-not [string]::Equals($taskCurrent,$taskNewDll,[StringComparison]::OrdinalIgnoreCase)) {
    $taskRestore = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\regsvr32.exe') -ArgumentList '/s',('"' + $taskOldDll + '"') -WindowStyle Hidden -Wait -PassThru
    if ($taskRestore.ExitCode -eq 0 -and [string]::Equals((Get-RegisteredDll),$taskOldDll,[StringComparison]::OrdinalIgnoreCase)) {
      Save-Status 'failed_old_restored' $taskReason
    } else { Save-Status 'failed_restore_needed' $taskReason }
  } else { Save-Status 'failed_new_registered' $taskReason }
  exit 1
}
