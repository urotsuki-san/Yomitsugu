# MozcとEDRDGの公開辞書を取得し、変換用のTSVを生成する。
param([string]$OutputPath = (Join-Path $env:LOCALAPPDATA 'ImeMixed\public_dictionary.tsv'), [string]$SourceDirectory = '',
      [string]$BundledPath = (Join-Path $PSScriptRoot 'engine\public_dictionary.tsv'))
$ErrorActionPreference = 'Stop'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$taskSources = [ordered]@{
  mozc_symbols = 'https://raw.githubusercontent.com/google/mozc/master/src/data/symbol/symbol.tsv'
  edrdg_edict2 = 'https://www.edrdg.org/pub/Nihongo/edict2u.gz'
}
function Get-SourceBytes([string]$url, [int]$maximum) {
  $taskRequest = [Net.HttpWebRequest][Net.WebRequest]::Create($url)
  $taskRequest.UserAgent = 'Yomitsugu-DictionaryUpdater/0.2'
  $taskRequest.Timeout = 45000
  $taskRequest.ReadWriteTimeout = 45000
  $taskRequest.AllowAutoRedirect = $true
  $taskResponse = $taskRequest.GetResponse()
  try {
    if ($taskResponse.ResponseUri.Scheme -ne 'https') { throw 'Dictionary source redirected outside HTTPS.' }
    $taskInput = $taskResponse.GetResponseStream()
    $taskOutput = New-Object IO.MemoryStream
    try {
      $taskBuffer = New-Object byte[] 65536
      while (($taskRead = $taskInput.Read($taskBuffer,0,$taskBuffer.Length)) -gt 0) {
        if ($taskOutput.Length + $taskRead -gt $maximum) { throw 'Dictionary source exceeds the size limit.' }
        $taskOutput.Write($taskBuffer,0,$taskRead)
      }
      return ,$taskOutput.ToArray()
    } finally { $taskOutput.Dispose(); $taskInput.Dispose() }
  } finally { $taskResponse.Dispose() }
}
function Get-Sha256([byte[]]$bytes) {
  $taskHash = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($taskHash.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
  finally { $taskHash.Dispose() }
}
function Write-Atomic([string]$path, [byte[]]$bytes) {
  $taskParent = [IO.Path]::GetDirectoryName($path)
  [IO.Directory]::CreateDirectory($taskParent) | Out-Null
  $taskTemp = Join-Path $taskParent ([IO.Path]::GetFileName($path) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
  $taskBackup = $taskTemp + '.bak'
  try {
    [IO.File]::WriteAllBytes($taskTemp,$bytes)
    if ([IO.File]::Exists($path)) { [IO.File]::Replace($taskTemp,$path,$taskBackup) }
    else { [IO.File]::Move($taskTemp,$path) }
  } finally {
    if ([IO.File]::Exists($taskTemp)) { [IO.File]::Delete($taskTemp) }
    if ([IO.File]::Exists($taskBackup)) { [IO.File]::Delete($taskBackup) }
  }
}
$taskMetaPath = [IO.Path]::ChangeExtension($OutputPath,'.sources.json')
$taskStatusPath = [IO.Path]::ChangeExtension($OutputPath,'.status.json')
$taskCheckedAt = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
$taskUtf8 = New-Object Text.UTF8Encoding($false)
$taskPreviousMetadata = $null
if ([IO.File]::Exists($taskMetaPath) -and (Get-Item -LiteralPath $taskMetaPath).Length -le 8192) {
  try { $taskPreviousMetadata = [IO.File]::ReadAllText($taskMetaPath) | ConvertFrom-Json } catch { }
}
try {
Add-Type -Path (Join-Path $PSScriptRoot 'PublicDictionaryBuilder.cs')
if ($SourceDirectory) {
  $taskSymbolBytes = [IO.File]::ReadAllBytes((Join-Path $SourceDirectory 'symbol.tsv'))
  $taskEdictBytes = [IO.File]::ReadAllBytes((Join-Path $SourceDirectory 'edict2u.gz'))
  if ($taskSymbolBytes.Length -gt 2000000 -or $taskEdictBytes.Length -gt 20000000) { throw 'Source exceeds size limit.' }
} else {
  $taskSymbolBytes = Get-SourceBytes $taskSources.mozc_symbols 2000000
  $taskEdictBytes = Get-SourceBytes $taskSources.edrdg_edict2 20000000
}
$taskEdictStream = New-Object IO.MemoryStream(,$taskEdictBytes)
$taskGzip = New-Object IO.Compression.GZipStream($taskEdictStream,[IO.Compression.CompressionMode]::Decompress)
$taskDecoded = New-Object IO.MemoryStream
try {
  $taskBuffer = New-Object byte[] 65536
  while (($taskRead = $taskGzip.Read($taskBuffer,0,$taskBuffer.Length)) -gt 0) {
    if ($taskDecoded.Length + $taskRead -gt 128MB) { throw 'Expanded dictionary exceeds size limit.' }
    $taskDecoded.Write($taskBuffer,0,$taskRead)
  }
  $taskGlossary = (New-Object Text.UTF8Encoding($false,$true)).GetString($taskDecoded.ToArray())
} finally { $taskDecoded.Dispose(); $taskGzip.Dispose(); $taskEdictStream.Dispose() }
$taskBuilt = [YomitsuguDictionaryBuilder]::Build($taskGlossary,[Text.Encoding]::UTF8.GetString($taskSymbolBytes))
$taskPayload = $taskUtf8.GetBytes($taskBuilt.Text)
if ($taskPayload.Length -gt 32MB) { throw 'Generated dictionary exceeds the engine limit.' }
$taskMetadata = [ordered]@{
  format_version=2
  source_urls=$taskSources
  source_sha256=[ordered]@{mozc_symbols=(Get-Sha256 $taskSymbolBytes); edrdg_edict2=(Get-Sha256 $taskEdictBytes)}
  output_sha256=(Get-Sha256 $taskPayload)
  readings=$taskBuilt.Readings
  symbols=$taskBuilt.Symbols
  computing_terms=$taskBuilt.ComputingTerms
  lexical_entries=$taskBuilt.LexicalEntries
  english_words=$taskBuilt.EnglishWords
  entries=$taskBuilt.Entries
}
$taskPriorFile = if ([IO.File]::Exists($OutputPath)) { $OutputPath } else { $BundledPath }
$taskPriorHash = ''
if ([IO.File]::Exists($taskPriorFile) -and (Get-Item -LiteralPath $taskPriorFile).Length -le 32MB) {
  $taskPriorHash = Get-Sha256 ([IO.File]::ReadAllBytes($taskPriorFile))
}
$taskChanged = $taskPriorHash -ne $taskMetadata.output_sha256
$taskCheckedAt = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
$taskMetadata.checked_at_utc = $taskCheckedAt
$taskMetadata.updated_at_utc = if ($taskChanged) { $taskCheckedAt } elseif ($taskPreviousMetadata.output_sha256 -eq $taskPriorHash) { $taskPreviousMetadata.updated_at_utc } else { $null }
$taskMetadata.update_result = if ($taskChanged) { 'updated' } else { 'unchanged' }
$taskMetaBytes = $taskUtf8.GetBytes(($taskMetadata | ConvertTo-Json -Depth 5) + "`n")
if ($taskChanged -or -not [IO.File]::Exists($OutputPath)) { Write-Atomic $OutputPath $taskPayload }
Write-Atomic $taskMetaPath $taskMetaBytes
$taskStatus = [ordered]@{format_version=1;checked_at_utc=$taskCheckedAt;result=$taskMetadata.update_result;output_sha256=$taskMetadata.output_sha256}
Write-Atomic $taskStatusPath ($taskUtf8.GetBytes(($taskStatus | ConvertTo-Json) + "`n"))
Write-Output ("Public dictionary: " + $taskMetadata.update_result)
Write-Output ($taskMetadata | ConvertTo-Json -Compress -Depth 5)
} catch {
  $taskFailure = [ordered]@{format_version=1;checked_at_utc=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ');result='failed'}
  try { Write-Atomic $taskStatusPath ($taskUtf8.GetBytes(($taskFailure | ConvertTo-Json) + "`n")) } catch { }
  throw
}
