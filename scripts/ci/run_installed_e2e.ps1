$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'This script runs only on the CI test desktop.'}
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskAudit=Join-Path $taskRoot 'audit/installed-e2e'
New-Item -ItemType Directory -Path $taskAudit -Force | Out-Null
$taskInstallers=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'audit/ci-installer') -Recurse -File -Filter 'Yomitsugu-0.2.9-preview-x64-setup.exe')
if($taskInstallers.Count -ne 1){throw 'Expected one installer'}
$taskSetup=$taskInstallers[0].FullName
$taskHash=(Get-FileHash -LiteralPath $taskSetup -Algorithm SHA256).Hash.ToLowerInvariant()
$taskInstallDir=Join-Path $env:ProgramFiles 'Yomitsugu/0.2.9-preview'
$taskDll=Join-Path $taskInstallDir 'ime_mixed_tip_v13.dll'
$taskClsid='Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
$taskUpdater=Join-Path $taskRoot 'native/build/Release/app_update_test.exe'
& $taskUpdater --launch-verified $taskSetup $taskHash
if($LASTEXITCODE -ne 0){throw 'Updater could not launch verified installer'}
$taskDeadline=[DateTime]::UtcNow.AddMinutes(3)
do {
    Start-Sleep -Seconds 1
    $taskRegistered=if(Test-Path -LiteralPath $taskClsid){(Get-Item -LiteralPath $taskClsid).GetValue('')}else{''}
    $taskRunning=@(Get-Process | Where-Object {$_.Path -eq $taskSetup})
    if($taskRegistered -eq $taskDll -and $taskRunning.Count -eq 0){break}
}while([DateTime]::UtcNow -lt $taskDeadline)
if($taskRegistered -ne $taskDll -or $taskRunning.Count -ne 0){throw 'Installer did not finish with the expected registration'}
python (Join-Path $PSScriptRoot 'installed_conversion.py') (Join-Path $taskInstallDir 'engine/ime_engine_host.exe')
if($LASTEXITCODE -ne 0){throw 'Installed engine conversion failed'}
$taskDriver=Join-Path $taskRoot 'native/build/e2e_out_v13/e2e_tip_tests.exe'
$taskResults=@()
foreach($taskCase in @(@{name='full';arguments=@()},@{name='reported';arguments=@('--reported-inputs')},@{name='learning';arguments=@('--learning-inputs')})){
    $taskLog=Join-Path $taskAudit ($taskCase.name+'.log')
    $taskStart=@{FilePath=$taskDriver;PassThru=$true;RedirectStandardOutput=$taskLog;RedirectStandardError=(Join-Path $taskAudit ($taskCase.name+'.err'))}
    if($taskCase.arguments.Count){$taskStart.ArgumentList=$taskCase.arguments}
    $taskProcess=Start-Process @taskStart
    $taskHandle=$taskProcess.Handle
    if(-not $taskProcess.WaitForExit(150000)){
        Stop-Process -Id $taskProcess.Id
        throw ('Input test timed out: '+$taskCase.name)
    }
    $taskLines=Get-Content -LiteralPath $taskLog -Encoding UTF8
    $taskLines | Select-String 'PASS|FAIL|E2E done|TIPDIAG'
    $taskResults+=@{name=$taskCase.name;exit=$taskProcess.ExitCode;summary=($taskLines | Select-String 'E2E done').Line}
}
@{installer_sha256=$taskHash;registered_dll=$taskRegistered;tests=$taskResults} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskAudit 'result.json') -Encoding UTF8
if(@($taskResults | Where-Object {$_.exit -ne 0}).Count){throw 'Installed input E2E failed'}
