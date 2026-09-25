$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskSource = Join-Path $taskRoot 'native/build/e2e_out_v11/engine'
$taskDestination = Join-Path $taskRoot 'native/build/Release'
if (-not (Test-Path -LiteralPath (Join-Path $taskSource 'azookey-engine.dll'))) { throw 'Staged conversion runtime missing.' }
if (-not (Test-Path -LiteralPath (Join-Path $taskDestination 'engine_test.exe'))) { throw 'Native tests missing.' }
foreach ($taskItem in (Get-ChildItem -LiteralPath $taskSource -Force)) {
  Copy-Item -LiteralPath $taskItem.FullName -Destination $taskDestination -Recurse -Force
}
Write-Output "Native test runtime staged at $taskDestination"
