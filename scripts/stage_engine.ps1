param([string]$Destination = 'native/build/e2e_out/engine')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskDestination = [IO.Path]::GetFullPath((Join-Path $taskRoot $Destination))
if (-not $taskDestination.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Destination must be inside this workspace' }
New-Item -ItemType Directory -Path $taskDestination -Force | Out-Null
$taskRuntime = Join-Path $taskRoot 'upstream/myime/build/x64/release'
foreach ($taskFile in (Get-ChildItem -LiteralPath $taskRuntime -Filter '*.dll' -File)) {
    Copy-Item -LiteralPath $taskFile.FullName -Destination $taskDestination -Force
}
foreach ($taskFolder in @('AzooKeyKanaKanjiConverter_EfficientNGram.bundle','AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary.bundle','models')) {
    $taskSource = Join-Path $taskRuntime $taskFolder
    if (-not (Test-Path -LiteralPath $taskSource)) { throw "Required asset missing: $taskFolder" }
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/Release/ime_engine_host.exe') -Destination $taskDestination -Force
$taskPublicDictionary = Join-Path $taskRoot 'native/assets/public_dictionary.tsv'
if (-not (Test-Path -LiteralPath $taskPublicDictionary)) { throw 'Public dictionary missing; run scripts/update_public_dictionary.ps1 -OutputPath native/assets/public_dictionary.tsv' }
Copy-Item -LiteralPath $taskPublicDictionary -Destination $taskDestination -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/assets/public_dictionary.sources.json') -Destination $taskDestination -Force
Copy-Item -LiteralPath $taskPublicDictionary -Destination (Join-Path $taskRoot 'native/build/Release/public_dictionary.tsv') -Force
Write-Output "Engine staged at $taskDestination"
