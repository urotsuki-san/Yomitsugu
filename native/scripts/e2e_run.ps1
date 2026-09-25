# E2E runner: elevate once, register, run tests (capture output), always unregister
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\e2e_out_v11\ime_mixed_tip_v11.dll'
$tests = Join-Path $root 'build\e2e_out_v11\e2e_tip_tests.exe'
$log = Join-Path $root 'scripts\e2e_result.txt'

function Log([string]$m) {
  $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
  Add-Content -Path $log -Value $line
  Write-Host $line
}

# Self-elevate first (no lock in parent), then lock only in elevated child
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  Log 'elevating...'
  try {
    $p = Start-Process powershell -Verb RunAs -WindowStyle Hidden -PassThru -ErrorAction Stop -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`""
  } catch {
    Log 'ELEVATION CANCELLED OR FAILED: tests were not run'
    exit 1223
  }
  $taskHandle = $p.Handle
  $p.WaitForExit()
  Log "elevated child exit=$($p.ExitCode)"
  exit $p.ExitCode
}

# single-instance lock after elevation (prevent concurrent register/unregister races)
$mutex = New-Object System.Threading.Mutex($false, 'Global\ImeE2ERun')
$gotLock = $false
try {
  $gotLock = $mutex.WaitOne(0)
} catch { $gotLock = $false }
if (-not $gotLock) {
  Write-Output 'E2E already running - exit 9'
  if ($mutex) { try { $mutex.Dispose() } catch {} }
  exit 9
}

Set-Content -Path $log -Value "E2E start $(Get-Date)"
Log "dll=$dll exists=$(Test-Path $dll)"
Log "tests=$tests exists=$(Test-Path $tests)"
if (-not (Test-Path $dll) -or -not (Test-Path $tests)) {
  Log 'MISSING ARTIFACTS'
  if ($mutex) { try { if ($gotLock) { $mutex.ReleaseMutex() } } catch {}; try { $mutex.Dispose() } catch {} }
  exit 2
}

$testCode = 99
try {
  Log '--- register ---'
  $reg = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/s',"`"$dll`"" -Wait -PassThru -NoNewWindow
  Log "regsvr32 exit=$($reg.ExitCode)"
  if ($reg.ExitCode -ne 0) { exit 3 }

  & icacls $dll /grant "*S-1-15-2-1:(RX)" 2>$null | Out-Null

  Log '--- run tests ---'
  $testLog = Join-Path $root 'scripts\e2e_tests_stdout.txt'
  if (Test-Path $testLog) { Remove-Item $testLog -Force -ErrorAction SilentlyContinue }
  if (Test-Path "$testLog.err") { Remove-Item "$testLog.err" -Force -ErrorAction SilentlyContinue }
  # give ctfmon time to pick up newly registered TIP
  Start-Sleep -Milliseconds 1500
  # This is a SendInput test against a real edit window. Keep its process visible
  # so Windows can focus that window and activate the TSF text service.
  $tp = Start-Process -FilePath $tests -PassThru -RedirectStandardOutput $testLog -RedirectStandardError "$testLog.err"
  $testHandle = $tp.Handle
  if (-not $tp.WaitForExit(120000)) {
    Stop-Process -Id $tp.Id -Force
    $testCode = 124
    Log 'TEST TIMEOUT (owned E2E process only)'
  } else { $testCode = $tp.ExitCode }
  Log "tests exit=$testCode"
  if (Test-Path $testLog) {
    Get-Content $testLog -Encoding UTF8 -ErrorAction SilentlyContinue | Select-String 'E2E done|\[FAIL\]' | ForEach-Object { Log "TEST| $_" }
  }
  if (Test-Path "$testLog.err") {
    $err = Get-Content "$testLog.err" -Raw -ErrorAction SilentlyContinue
    if ($err) { Log "TESTERR| $err" }
  }
} finally {
  Log '--- unregister (always) ---'
  try {
    $ur = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/u','/s',"`"$dll`"" -Wait -PassThru -NoNewWindow
    Log "unregsvr32 exit=$($ur.ExitCode)"
  } catch { Log "unreg error: $_" }

  $clsid = '{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}'
  foreach ($pk in @(
    "HKLM:\SOFTWARE\Classes\CLSID\$clsid",
    "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid",
    "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid",
    "HKCU:\SOFTWARE\Microsoft\CTF\TIP\$clsid"
  )) {
    if (Test-Path $pk) { Remove-Item $pk -Recurse -Force -ErrorAction SilentlyContinue; Log "removed $pk" }
  }

  $restore = Join-Path $root 'build\Release\ime_profile_control.exe'
  & $restore --restore-google
  if ($LASTEXITCODE -ne 0) { Log 'RESTORE GOOGLE FAILED'; $testCode = 125 }
  else {
    & $restore --check-google
    if ($LASTEXITCODE -ne 0) { Log 'DEFAULT GOOGLE VERIFICATION FAILED'; $testCode = 125 }
    else { Log 'default ja IME = Google Japanese Input (TSF API, verified)' }
  }

  Log 'cleanup done'
}

$code = if ($testCode -eq 0) { 0 } else { 1 }
Log "E2E finish code=$code"
if ($mutex) {
  try { if ($gotLock) { $mutex.ReleaseMutex() } } catch {}
  try { $mutex.Dispose() } catch {}
}
exit $code
