# Windows PowerShell 5.1 / .NET Framework. Downloads dictionaries only; no input text is sent.
param([string]$OutputPath = (Join-Path $env:LOCALAPPDATA 'ImeMixed\public_dictionary.tsv'))
$ErrorActionPreference = 'Stop'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$taskSources = [ordered]@{
  mozc_symbols = 'https://raw.githubusercontent.com/google/mozc/master/src/data/symbol/symbol.tsv'
  edrdg_edict2 = 'https://www.edrdg.org/pub/Nihongo/edict2.gz'
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
function Is-Kana([string]$reading) { return $reading -cmatch '^[ぁ-ゖー]{2,30}$' }
$taskRows = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.List[object]]' ([StringComparer]::Ordinal)
function Add-Entry([string]$reading, [string]$surface, [string]$pos) {
  if (-not (Is-Kana $reading) -or -not $surface -or $surface.Length -gt 48) { return $false }
  $taskValues = $null
  if (-not $taskRows.TryGetValue($reading,[ref]$taskValues)) {
    $taskValues = New-Object 'System.Collections.Generic.List[object]'
    $taskRows.Add($reading,$taskValues)
  }
  if ($taskValues.Count -ge 32) { return $false }
  foreach ($taskValue in $taskValues) {
    if ([string]::Equals($taskValue.Surface,$surface,[StringComparison]::Ordinal)) { return $false }
  }
  $taskValues.Add([pscustomobject]@{Surface=$surface; Pos=$pos})
  return $true
}
$taskSymbolBytes = Get-SourceBytes $taskSources.mozc_symbols 2000000
$taskEdictBytes = Get-SourceBytes $taskSources.edrdg_edict2 20000000
$taskEdictStream = New-Object IO.MemoryStream(,$taskEdictBytes)
$taskGzip = New-Object IO.Compression.GZipStream($taskEdictStream,[IO.Compression.CompressionMode]::Decompress)
$taskDecoded = New-Object IO.MemoryStream
try { $taskGzip.CopyTo($taskDecoded); $taskGlossary = [Text.Encoding]::GetEncoding('euc-jp').GetString($taskDecoded.ToArray()) }
finally { $taskDecoded.Dispose(); $taskGzip.Dispose(); $taskEdictStream.Dispose() }
$taskTerms = 0
foreach ($taskLine in ($taskGlossary -split "`n")) {
  if (-not $taskLine.Contains('{comp}')) { continue }
  $taskSplit = $taskLine.IndexOf(' /')
  if ($taskSplit -lt 0) { continue }
  $taskHead = $taskLine.Substring(0,$taskSplit)
  $taskMatch = [regex]::Match($taskHead,'\[([^]]+)\]')
  $taskReading = if ($taskMatch.Success) { $taskMatch.Groups[1].Value } else { $taskHead.Split(';')[0].Trim() }
  $taskReading = [regex]::Replace($taskReading,'[ァ-ヶ]',{param($m) [string][char]([int][char]$m.Value - 0x60)})
  if (-not (Is-Kana $taskReading)) { continue }
  foreach ($taskGloss in ($taskLine.Substring($taskSplit+2) -split '/')) {
    $taskWord = ([regex]::Replace($taskGloss,'\([^)]*\)|\{[^}]*\}','')).Trim()
    if ($taskWord -cmatch '^[A-Z][A-Z0-9+._-]{1,30}$') {
      if (Add-Entry $taskReading $taskWord '名詞') { $taskTerms++ }
    }
  }
}
$taskSymbolText = [Text.Encoding]::UTF8.GetString($taskSymbolBytes).TrimStart([char]0xfeff)
$taskSymbols = 0
$taskFirst = $true
foreach ($taskLine in ($taskSymbolText -split "`n")) {
  if ($taskFirst) { $taskFirst = $false; continue }
  $taskColumns = $taskLine.TrimEnd("`r") -split "`t"
  if ($taskColumns.Count -lt 3) { continue }
  foreach ($taskReading in ($taskColumns[2] -split '\s+')) {
    if (Add-Entry $taskReading $taskColumns[1] '記号') { $taskSymbols++ }
  }
}
if ($taskSymbols -lt 500 -or $taskSymbols -gt 20000 -or $taskTerms -lt 10 -or $taskTerms -gt 10000) {
  throw "Source coverage changed: symbols=$taskSymbols terms=$taskTerms"
}
foreach ($taskRequired in @(@('りーどみー','README'),@('やじるし','→'),@('まる','○'))) {
  $taskFound = $false
  $taskValues = $null
  if ($taskRows.TryGetValue($taskRequired[0],[ref]$taskValues)) {
    foreach ($taskValue in $taskValues) {
      if ([string]::Equals($taskValue.Surface,$taskRequired[1],[StringComparison]::Ordinal)) { $taskFound = $true; break }
    }
  }
  if (-not $taskFound) { throw "Required entry missing: $($taskRequired[0])" }
}
$taskKeys = [string[]]@($taskRows.Keys)
[Array]::Sort($taskKeys,[StringComparer]::Ordinal)
$taskBuilder = New-Object Text.StringBuilder
[void]$taskBuilder.Append("# Sources: Mozc symbol.tsv (BSD-3-Clause); EDRDG EDICT2 (CC BY-SA 4.0)`n")
$taskEntries = 0
foreach ($taskReading in $taskKeys) {
  foreach ($taskValue in $taskRows[$taskReading]) {
    [void]$taskBuilder.Append($taskReading).Append("`t").Append($taskValue.Surface).Append("`t").Append($taskValue.Pos).Append("`n")
    $taskEntries++
  }
}
$taskUtf8 = New-Object Text.UTF8Encoding($false)
$taskPayload = $taskUtf8.GetBytes($taskBuilder.ToString())
if ($taskPayload.Length -gt 4MB) { throw 'Generated dictionary exceeds the engine limit.' }
$taskMetadata = [ordered]@{
  source_urls=$taskSources
  source_sha256=[ordered]@{mozc_symbols=(Get-Sha256 $taskSymbolBytes); edrdg_edict2=(Get-Sha256 $taskEdictBytes)}
  output_sha256=(Get-Sha256 $taskPayload)
  readings=$taskRows.Count
  symbols=$taskSymbols
  computing_terms=$taskTerms
  entries=$taskEntries
}
$taskMetaPath = [IO.Path]::ChangeExtension($OutputPath,'.sources.json')
$taskMetaBytes = $taskUtf8.GetBytes(($taskMetadata | ConvertTo-Json -Depth 5) + "`n")
Write-Atomic $OutputPath $taskPayload
Write-Atomic $taskMetaPath $taskMetaBytes
Write-Output "Updated public dictionary: $OutputPath"
Write-Output "readings=$($taskRows.Count) symbols=$taskSymbols terms=$taskTerms entries=$taskEntries"
