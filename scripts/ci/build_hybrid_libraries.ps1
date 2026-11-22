param([string]$BuildDirectory = (Join-Path $PSScriptRoot '../../native/build/hybrid-deps'))
$ErrorActionPreference = 'Stop'
$revision = '57291f2644af8c9df0dd8d44395881c5bdcf0ecd'
$sha256 = '7EE2C778719D43FB0C05F19413D3CD6C44C1C2AFB15335F7A2F70AA55F77EAB1'
$vendor = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../native/third_party/iroha')).Path
New-Item -Path $BuildDirectory -ItemType Directory -Force | Out-Null
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$archive = Join-Path $buildRoot 'llama.tar.gz'
if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive).Hash -ne $sha256) {
    curl.exe --fail --location --retry 3 --output $archive "https://codeload.github.com/ggml-org/llama.cpp/tar.gz/$revision"
    if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $archive).Hash -ne $sha256) { throw 'llama.cpp archive verification failed.' }
}
$source = Join-Path $buildRoot "llama.cpp-$revision"
tar.exe -xzf $archive -C $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'Could not extract llama.cpp.' }
Push-Location -LiteralPath $source
try {
    git init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the patch directory.' }
    foreach ($patch in 'llama-cpp-t5-cross-kv.patch','llama-cpp-zenz-pretokenizer.patch') {
        git -c core.autocrlf=false apply --whitespace=nowarn (Join-Path $vendor "patches/$patch")
        if ($LASTEXITCODE -ne 0) { throw "Could not apply $patch." }
    }
} finally { Pop-Location }
$binary = Join-Path $buildRoot 'build'
cmake -S $source -B $binary -A x64 -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_AVX2=ON -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_MTMD=OFF
if ($LASTEXITCODE -ne 0) { throw 'Could not configure llama.cpp.' }
cmake --build $binary --config Release --target llama --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Could not build llama.cpp.' }
$destination = Join-Path $vendor 'dist-windows/lib'
New-Item -Path $destination -ItemType Directory -Force | Out-Null
foreach ($lib in 'src/Release/llama.lib','ggml/src/Release/ggml.lib','ggml/src/Release/ggml-cpu.lib','ggml/src/Release/ggml-base.lib') {
    Copy-Item -LiteralPath (Join-Path $binary $lib) -Destination $destination -Force
}
