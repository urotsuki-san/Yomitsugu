$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskMyime = Join-Path $taskRoot 'upstream/myime'
$taskOut = Join-Path $taskMyime 'build/x64/release'
$taskLlama = Join-Path $taskMyime 'llama.cpp-src'
$taskLlamaBuild = Join-Path $taskLlama 'build'
$taskLibs = Join-Path $taskMyime 'src/AzooKeyKanaKanjiConverter/lib/windows'
New-Item -ItemType Directory -Path (Split-Path $taskMyime) -Force | Out-Null

git clone --filter=blob:none https://github.com/unok/myime.git $taskMyime
if ($LASTEXITCODE -ne 0) { throw 'myime clone failed.' }
git -C $taskMyime checkout a8486eca5312556ff88fed7f1850a28843b67977
if ($LASTEXITCODE -ne 0) { throw 'myime pinned checkout failed.' }
$taskDictionarySubmodule = 'src/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_dictionary_storage'
$taskEmojiSubmodule = 'src/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_emoji_dictionary_storage'
git -C $taskMyime submodule update --init --depth 1 -- $taskDictionarySubmodule $taskEmojiSubmodule
if ($LASTEXITCODE -ne 0) { throw 'AzooKey data submodule checkout failed.' }
New-Item -ItemType Directory -Path $taskOut,$taskLibs -Force | Out-Null

git clone --depth 1 --branch b4846 https://github.com/fkunn1326/llama.cpp.git $taskLlama
if ($LASTEXITCODE -ne 0) { throw 'llama.cpp checkout failed.' }
cmake -S $taskLlama -B $taskLlamaBuild -G 'Visual Studio 17 2022' -A x64 `
  -DGGML_VULKAN=OFF -DGGML_BACKEND_DL=ON -DGGML_NATIVE=OFF -DGGML_AVX2=ON `
  -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF -DBUILD_SHARED_LIBS=ON
if ($LASTEXITCODE -ne 0) { throw 'llama.cpp configure failed.' }
cmake --build $taskLlamaBuild --config Release --parallel --target llama ggml ggml-base ggml-cpu
if ($LASTEXITCODE -ne 0) { throw 'llama.cpp build failed.' }
$taskLlamaBin = Join-Path $taskLlamaBuild 'bin/Release'
foreach ($taskName in @('ggml.dll','ggml-base.dll','ggml-cpu.dll','llama.dll')) {
  $taskFile = Join-Path $taskLlamaBin $taskName
  if (-not (Test-Path -LiteralPath $taskFile)) { throw "Missing llama runtime: $taskName" }
  Copy-Item -LiteralPath $taskFile -Destination $taskOut -Force
  Copy-Item -LiteralPath $taskFile -Destination $taskLibs -Force
}
foreach ($taskName in @('llama.lib','ggml.lib','ggml-base.lib','ggml-cpu.lib')) {
  $taskHit = Get-ChildItem -LiteralPath $taskLlamaBuild -Recurse -File -Filter $taskName | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
  if ($taskHit) { Copy-Item -LiteralPath $taskHit.FullName -Destination $taskLibs -Force }
}

$taskSwiftPackage = Join-Path $taskMyime 'src/swift-engine'
Push-Location $taskSwiftPackage
try {
  swift build -c release --arch x86_64
  if ($LASTEXITCODE -ne 0) { throw 'Swift engine build failed.' }
} finally { Pop-Location }
$taskSwiftBuild = Join-Path $taskSwiftPackage '.build'
$taskEngineDll = Get-ChildItem -LiteralPath $taskSwiftBuild -Recurse -File -Filter 'azookey-engine.dll' |
  Where-Object { $_.FullName -match '\\release\\' } | Select-Object -First 1
if (-not $taskEngineDll) { throw 'Built azookey-engine.dll missing.' }
Copy-Item -LiteralPath $taskEngineDll.FullName -Destination $taskOut -Force
foreach ($taskStem in @('AzooKeyKanaKanjiConverter_EfficientNGram','AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary')) {
  $taskBundle = Get-ChildItem -LiteralPath $taskSwiftBuild -Recurse -Directory |
    Where-Object { $_.Name -in @("$taskStem.bundle","$taskStem.resources") } | Select-Object -First 1
  if (-not $taskBundle) { throw "Swift resource bundle missing: $taskStem" }
  Copy-Item -LiteralPath $taskBundle.FullName -Destination (Join-Path $taskOut $taskBundle.Name) -Recurse -Force
}
& (Join-Path $taskMyime 'scripts/ci/copy-swift-runtime.ps1') -OutputDir $taskOut
foreach ($taskName in @('swiftCore.dll','Foundation.dll','_FoundationICU.dll','dispatch.dll')) {
  if (-not (Test-Path -LiteralPath (Join-Path $taskOut $taskName))) { throw "Swift runtime missing: $taskName" }
}

$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVsRoot) { throw 'Visual Studio C++ toolchain missing.' }
$taskCrt = Get-ChildItem -LiteralPath (Join-Path $taskVsRoot 'VC/Redist/MSVC') -Recurse -Directory -Filter 'Microsoft.VC143.CRT' |
  Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $taskCrt) { throw 'Visual C++ x64 redistributable directory missing.' }
Get-ChildItem -LiteralPath $taskCrt.FullName -File -Filter '*.dll' | Copy-Item -Destination $taskOut -Force

$taskModels = Join-Path $taskOut 'models'
New-Item -ItemType Directory -Path $taskModels -Force | Out-Null
$taskModel = Join-Path $taskModels 'ggml-model-Q5_K_M.gguf'
$taskModelUrl = 'https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/resolve/c67e03e07d215c869f591b274c1631170d3e11fe/ggml-model-Q5_K_M.gguf'
curl.exe -L --fail --retry 3 --output $taskModel $taskModelUrl
if ($LASTEXITCODE -ne 0) { throw 'Zenzai model download failed.' }
$taskModelHash = (Get-FileHash -LiteralPath $taskModel -Algorithm SHA256).Hash
if ($taskModelHash -ne '29C223D4C23327B80FD13EBB5AB2555057A46317997D5DA391584FFBEF0DB673') { throw "Zenzai model hash mismatch: $taskModelHash" }
Write-Output "Runtime ready: $taskOut"
