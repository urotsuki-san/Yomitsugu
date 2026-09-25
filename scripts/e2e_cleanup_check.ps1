$ErrorActionPreference = 'Stop'
$clsid = '{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}'
$paths = @(
  "HKLM:\SOFTWARE\Classes\CLSID\$clsid",
  "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid",
  "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid",
  "HKCU:\SOFTWARE\Microsoft\CTF\TIP\$clsid"
)
$remaining = @($paths | Where-Object { Test-Path -LiteralPath $_ })
Write-Output "TIP registry entries remaining=$($remaining.Count)"
if ($remaining.Count -ne 0) { exit 1 }
