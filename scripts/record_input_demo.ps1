param([switch]$AllowDesktopInput)
$ErrorActionPreference = 'Stop'
if (-not $AllowDesktopInput) {
  throw 'This script opens a visible Rich Edit window and types into it. Run again with -AllowDesktopInput only when the desktop is free.'
}
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskExe = Join-Path $taskRoot 'native/build/e2e_out_v13/e2e_tip_tests.exe'
$taskAudit = Join-Path $taskRoot 'audit/2026-09-26/readme-demo'
New-Item -ItemType Directory -Path $taskAudit -Force | Out-Null
$taskStdout = Join-Path $taskAudit 'demo.out.txt'
$taskStderr = Join-Path $taskAudit 'demo.err.txt'
$taskDemo = Start-Process -FilePath $taskExe -ArgumentList '--demo' -PassThru -Wait -RedirectStandardOutput $taskStdout -RedirectStandardError $taskStderr
if ($taskDemo.ExitCode -ne 0) { throw "Demo conversion failed; see $taskStdout" }
Write-Output "Captured verified TIP events: $taskStdout"
Write-Output 'The GIF can now be rendered offline with: uv run --with pillow python scripts/render_input_demo.py'
