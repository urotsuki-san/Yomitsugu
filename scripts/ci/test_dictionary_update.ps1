param([string]$SourceDirectory = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskAudit = Join-Path $taskRoot ('audit/dictionary-status-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($taskAudit) | Out-Null
$taskUpdater = Join-Path $taskRoot 'scripts/update_public_dictionary.ps1'
$taskPowerShell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$taskOutput = Join-Path $taskAudit 'public_dictionary.tsv'
$taskMetadata = [IO.Path]::ChangeExtension($taskOutput, '.sources.json')
$taskStatus = [IO.Path]::ChangeExtension($taskOutput, '.status.json')
$taskUtf8 = New-Object Text.UTF8Encoding($false)
if (-not $SourceDirectory) {
    $SourceDirectory = Join-Path $taskAudit 'sources'
    [IO.Directory]::CreateDirectory($SourceDirectory) | Out-Null
    Invoke-WebRequest 'https://raw.githubusercontent.com/google/mozc/master/src/data/symbol/symbol.tsv' -OutFile (Join-Path $SourceDirectory 'symbol.tsv') -UseBasicParsing
    Invoke-WebRequest 'https://www.edrdg.org/pub/Nihongo/edict2u.gz' -OutFile (Join-Path $SourceDirectory 'edict2u.gz') -UseBasicParsing
}
function Invoke-Update([string]$Name, [string]$Output, [string]$Sources, [string]$Bundle, [bool]$ExpectFailure = $false) {
    if (-not $Bundle) { $Bundle = Join-Path $taskAudit 'no-bundle.tsv' }
    $taskArguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $taskUpdater + '" -OutputPath "' + $Output + '" -SourceDirectory "' + $Sources + '" -BundledPath "' + $Bundle + '"'
    $taskProcess = Start-Process -FilePath $taskPowerShell -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $taskAudit ($Name + '.log')) -RedirectStandardError (Join-Path $taskAudit ($Name + '.err'))
    if (($taskProcess.ExitCode -ne 0) -ne $ExpectFailure) {
        Get-Content -LiteralPath (Join-Path $taskAudit ($Name + '.err'))
        throw "Unexpected updater result: $Name ($($taskProcess.ExitCode)); logs: $taskAudit"
    }
}
function Assert-Status([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    Write-Output ('PASS ' + $Message)
}
function Utc-Stamp($Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') }
    return [string]$Value
}
Invoke-Update 'first' $taskOutput $SourceDirectory (Join-Path $taskAudit 'no-bundle.tsv')
$taskFirst = Get-Content -LiteralPath $taskMetadata -Raw | ConvertFrom-Json
Assert-Status ($taskFirst.update_result -eq 'updated' -and $taskFirst.checked_at_utc -eq $taskFirst.updated_at_utc) 'First update records both dates'
$taskHash = (Get-FileHash -LiteralPath $taskOutput -Algorithm SHA256).Hash
$taskMtime = (Get-Item -LiteralPath $taskOutput).LastWriteTimeUtc
$taskFirst.updated_at_utc = '2026-01-01T00:00:00Z'
[IO.File]::WriteAllText($taskMetadata, ($taskFirst | ConvertTo-Json -Depth 5), $taskUtf8)
Invoke-Update 'unchanged' $taskOutput $SourceDirectory ''
$taskSame = Get-Content -LiteralPath $taskMetadata -Raw | ConvertFrom-Json
Assert-Status ($taskSame.update_result -eq 'unchanged' -and (Utc-Stamp $taskSame.updated_at_utc) -eq '2026-01-01T00:00:00Z') 'Unchanged check preserves update date'
Assert-Status ($taskSame.checked_at_utc -ge $taskFirst.checked_at_utc -and $taskSame.checked_at_utc -ne $taskSame.updated_at_utc) 'Latest check is recorded separately'
Assert-Status ((Get-Item -LiteralPath $taskOutput).LastWriteTimeUtc -eq $taskMtime) 'Unchanged dictionary is not rewritten'
$taskMetadataHash = (Get-FileHash -LiteralPath $taskMetadata -Algorithm SHA256).Hash
Invoke-Update 'failed' $taskOutput (Join-Path $taskAudit 'missing-source') '' $true
$taskFailed = Get-Content -LiteralPath $taskStatus -Raw | ConvertFrom-Json
Assert-Status ($taskFailed.result -eq 'failed' -and $taskFailed.checked_at_utc -ge $taskSame.checked_at_utc) 'Failed check records the attempt'
Assert-Status ((Get-FileHash -LiteralPath $taskOutput -Algorithm SHA256).Hash -eq $taskHash -and (Get-FileHash -LiteralPath $taskMetadata -Algorithm SHA256).Hash -eq $taskMetadataHash) 'Failure preserves dictionary and successful update metadata'
Invoke-Update 'recover' $taskOutput $SourceDirectory ''
$taskRecovered = Get-Content -LiteralPath $taskStatus -Raw | ConvertFrom-Json
Assert-Status ($taskRecovered.result -eq 'unchanged') 'Successful retry clears failure status'
$taskBundleCheck = Join-Path $taskAudit 'bundled-check.tsv'
Invoke-Update 'bundled' $taskBundleCheck $SourceDirectory $taskOutput
$taskBundled = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskBundleCheck, '.sources.json')) -Raw | ConvertFrom-Json
Assert-Status ($taskBundled.update_result -eq 'unchanged' -and -not $taskBundled.updated_at_utc -and $taskBundled.checked_at_utc) 'Unchanged bundled dictionary records only a check'
Write-Output ('Dictionary update status: 8/8; logs: ' + $taskAudit)
