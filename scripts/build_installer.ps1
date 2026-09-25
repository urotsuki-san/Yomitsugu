$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskCandidates = @(
  (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
  (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
  (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe')
)
$taskIscc = $taskCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $taskIscc) {
  $taskCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($taskCommand) { $taskIscc = $taskCommand.Source }
}
if (-not $taskIscc) { throw 'Inno Setup 6 (ISCC.exe) is required.' }
& (Join-Path $taskRoot 'scripts/validate_package.ps1')
$taskLogDir = Join-Path $taskRoot 'audit/2026-09-26/installer'
New-Item -ItemType Directory -Path $taskLogDir -Force | Out-Null
$taskCompileLog = Join-Path $taskLogDir 'iscc.log'
& $taskIscc (Join-Path $taskRoot 'installer/Yomitsugu.iss') *> $taskCompileLog
if ($LASTEXITCODE -ne 0) {
  Get-Content -LiteralPath $taskCompileLog -Tail 15
  throw 'Inno Setup compilation failed.'
}
Get-Content -LiteralPath $taskCompileLog -Tail 5
$taskInstaller = Join-Path $taskRoot 'dist/installer/Yomitsugu-0.2.7-preview-x64-setup.exe'
if (-not (Test-Path -LiteralPath $taskInstaller)) { throw 'Installer output missing.' }
Get-Item -LiteralPath $taskInstaller | Select-Object FullName,Length
Write-Output "SHA256=$((Get-FileHash -LiteralPath $taskInstaller -Algorithm SHA256).Hash)"
