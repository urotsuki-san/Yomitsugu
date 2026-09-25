$ErrorActionPreference = 'Stop'
$taskPath = Join-Path $PSScriptRoot 'upgrade_0_2_7.ps1'
$taskTokens = $null
$taskErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile($taskPath, [ref]$taskTokens, [ref]$taskErrors)
if ($taskErrors.Count -ne 0) {
  $taskErrors | ForEach-Object { Write-Error $_.Message }
  exit 1
}
Write-Output 'Upgrade workflow syntax passed'
