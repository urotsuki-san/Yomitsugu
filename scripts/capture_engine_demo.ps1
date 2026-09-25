$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskExe = Join-Path $taskRoot 'native/build/Release/debug_dump.exe'
$taskAudit = Join-Path $taskRoot 'audit/2026-09-26/readme-demo'
New-Item -ItemType Directory -Path $taskAudit -Force | Out-Null
$taskLog = Join-Path $taskAudit 'engine-demo.out.txt'
$taskErrorLog = Join-Path $taskAudit 'engine-demo.err.txt'
$taskRun = Start-Process -FilePath $taskExe -ArgumentList '--readme-demo' -PassThru -Wait -WindowStyle Hidden -RedirectStandardOutput $taskLog -RedirectStandardError $taskErrorLog
if ($taskRun.ExitCode -ne 0) { throw "Engine demo failed; see $taskLog" }
Write-Output "Captured conversion-engine candidates without desktop input: $taskLog"
