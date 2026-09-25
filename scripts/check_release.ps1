param([ValidateSet('native','python','release')][string]$Mode = 'native', [string]$HostPath = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $taskRoot
$taskOut = Join-Path $taskRoot 'audit/2026-09-25/release'
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
if ($Mode -eq 'python') {
    uv run pytest -q
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    uv run python audit/2026-09-25/seed_summary.py
    exit $LASTEXITCODE
}
$taskNames = @('engine_test','quality_typo_long')
if ($Mode -eq 'release') { $taskNames = @('release_test') }
$taskFailures = 0
foreach ($taskName in $taskNames) {
    $taskOptions = @{
        FilePath="$taskRoot/native/build/Release/$taskName.exe"; WindowStyle='Hidden'; PassThru=$true;
        RedirectStandardOutput="$taskOut/$taskName.log"; RedirectStandardError="$taskOut/$taskName.err"
    }
    if ($HostPath) { $taskOptions.ArgumentList = @('"' + $HostPath + '"') }
    $taskProcess = Start-Process @taskOptions
    $taskHandle = $taskProcess.Handle
    if (-not $taskProcess.WaitForExit(120000)) { Stop-Process -Id $taskProcess.Id; throw "$taskName timed out" }
    $taskProcess.Refresh()
    $taskProcess.WaitForExit()
    $taskCode = $taskProcess.ExitCode
    if ($null -eq $taskCode) { $taskCode = -1 }
    Get-Content "$taskOut/$taskName.log" -Encoding UTF8 | Select-String 'ALL PASSED|FAILED|SUMMARY|NOT ready|\[FAIL\]|\[SKIP\]|latency|^PASS|^FAIL|^TOTAL'
    Write-Output "$taskName exit=$taskCode"
    if ($taskCode -ne 0) { $taskFailures++ }
}
exit $taskFailures
