param([string]$Destination = 'native/build/e2e_out/engine', [string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskDestination = [IO.Path]::GetFullPath((Join-Path $taskRoot $Destination))
if (-not $taskDestination.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Destination must be inside this workspace' }
New-Item -ItemType Directory -Path $taskDestination -Force | Out-Null
$taskRuntime = if ($RuntimeSource) { (Resolve-Path -LiteralPath $RuntimeSource).Path } else { Join-Path $taskRoot 'upstream/myime/build/x64/release' }
foreach ($taskFile in (Get-ChildItem -LiteralPath $taskRuntime -Filter '*.dll' -File)) {
    Copy-Item -LiteralPath $taskFile.FullName -Destination $taskDestination -Force
}
foreach ($taskStem in @('AzooKeyKanaKanjiConverter_EfficientNGram','AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary')) {
    $taskBundles = @('bundle','resources') | ForEach-Object { Join-Path $taskRuntime ($taskStem + '.' + $_) } | Where-Object { Test-Path -LiteralPath $_ }
    if (-not $taskBundles) { throw "Required resource directory missing: $taskStem" }
    foreach ($taskSource in $taskBundles) { Copy-Item -LiteralPath $taskSource -Destination $taskDestination -Recurse -Force }
}
$taskModels = Join-Path $taskRuntime 'models'
if (-not (Test-Path -LiteralPath $taskModels)) { throw 'Required model directory missing' }
Copy-Item -LiteralPath $taskModels -Destination $taskDestination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/build/Release/ime_engine_host.exe') -Destination $taskDestination -Force
$taskPublicDictionary = Join-Path $taskRoot 'native/assets/public_dictionary.tsv'
if (-not (Test-Path -LiteralPath $taskPublicDictionary)) { throw 'Public dictionary missing; run scripts/update_public_dictionary.ps1 -OutputPath native/assets/public_dictionary.tsv' }
Copy-Item -LiteralPath $taskPublicDictionary -Destination $taskDestination -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/assets/public_dictionary.sources.json') -Destination $taskDestination -Force
Copy-Item -LiteralPath $taskPublicDictionary -Destination (Join-Path $taskRoot 'native/build/Release/public_dictionary.tsv') -Force
Write-Output "Engine staged at $taskDestination"
