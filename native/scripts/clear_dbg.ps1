$p = Join-Path $PSScriptRoot 'tip_debug.txt'
if (Test-Path $p) { Remove-Item $p -Force }
Write-Output 'cleared tip_debug'
