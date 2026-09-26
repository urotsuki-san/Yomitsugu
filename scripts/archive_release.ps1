$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskPackage = Join-Path $taskRoot 'dist/yomitsugu-0.2.9-preview'
$taskArchive = Join-Path $taskRoot 'dist/yomitsugu-0.2.9-preview.zip'
if (Test-Path -LiteralPath $taskArchive) { throw 'Archive already exists' }
if ((Get-FileHash -LiteralPath (Join-Path $taskRoot 'RELEASE.md') -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $taskPackage 'README.md') -Algorithm SHA256).Hash) {
    throw 'Packaged README is stale; refresh it and its manifest before archiving.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($taskPackage,$taskArchive,[IO.Compression.CompressionLevel]::Optimal,$true)
$taskHash = Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256
$taskHash.Hash | Set-Content -LiteralPath ($taskArchive + '.sha256') -Encoding ASCII
Get-Item -LiteralPath $taskArchive | Select-Object Name,Length
Write-Output "SHA256=$($taskHash.Hash)"
