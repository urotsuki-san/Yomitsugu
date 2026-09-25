$ErrorActionPreference = 'Stop'
$taskCurrent = $PID
$taskOther = Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $taskCurrent -and ($_.Name -eq 'e2e_tip_tests.exe' -or
    ($_.Name -eq 'powershell.exe' -and $_.CommandLine -match '\be2e_run\.ps1\b'))
}
if ($taskOther) { throw 'An E2E process is already running; do not launch another.' }
Write-Output 'E2E idle confirmed'
$taskRequired = @('native/build/e2e_out_v12/ime_mixed_tip_v12.dll','native/build/e2e_out_v12/e2e_tip_tests.exe',
  'native/build/e2e_out_v12/tip_com_smoke.exe',
  'native/build/e2e_out_v12/engine/ime_engine_host.exe','native/build/e2e_out_v12/engine/public_dictionary.tsv',
  'native/build/Release/ime_profile_control.exe')
foreach ($taskFile in $taskRequired) { if (-not (Test-Path -LiteralPath $taskFile)) { throw "Missing: $taskFile" } }
Write-Output 'E2E artifacts confirmed'
& 'native/build/e2e_out_v12/tip_com_smoke.exe'
if ($LASTEXITCODE -ne 0) { throw 'TIP COM smoke failed.' }
& 'native/build/e2e_out_v12/e2e_tip_tests.exe' --tsf-smoke
if ($LASTEXITCODE -ne 0) { throw 'TSF thread activation smoke failed.' }
& 'native/build/e2e_out_v12/e2e_tip_tests.exe' --richedit-smoke
if ($LASTEXITCODE -ne 0) { throw 'RichEdit TSF smoke failed.' }
$taskParseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile(
  (Resolve-Path -LiteralPath 'native/scripts/e2e_run.ps1'),
  [ref]$null,
  [ref]$taskParseErrors) | Out-Null
if ($taskParseErrors.Count -ne 0) { throw 'E2E runner has PowerShell parse errors.' }
Write-Output 'E2E runner syntax confirmed'
