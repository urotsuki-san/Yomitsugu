$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'This script runs only on the CI test desktop.'}
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskAudit=Join-Path $taskRoot 'audit/installed-e2e'
New-Item -ItemType Directory -Path $taskAudit -Force | Out-Null
$taskInstallers=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'audit/ci-installer') -Recurse -File -Filter 'Yomitsugu-0.2.10-preview-x64-setup.exe')
if($taskInstallers.Count -ne 1){throw 'Expected one installer'}
$taskSetup=$taskInstallers[0].FullName
$taskHash=(Get-FileHash -LiteralPath $taskSetup -Algorithm SHA256).Hash.ToLowerInvariant()
$taskInstallDir=Join-Path $env:ProgramFiles 'Yomitsugu/0.2.10-preview'
$taskDll=Join-Path $taskInstallDir 'ime_mixed_tip_v14.dll'
$taskClsid='Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
$taskUpdater=Join-Path $taskRoot 'native/build/Release/app_update_test.exe'

# 旧版へ辞書と学習を置き、新版インストーラーによる引き継ぎを確認する。
$taskOldSetup=Join-Path $taskAudit 'Yomitsugu-0.2.9-preview-x64-setup.exe'
Invoke-WebRequest 'https://github.com/urotsuki-san/Yomitsugu/releases/download/v0.2.9-preview.1/Yomitsugu-0.2.9-preview-x64-setup.exe' -OutFile $taskOldSetup
$taskOldHash=(Get-FileHash -LiteralPath $taskOldSetup -Algorithm SHA256).Hash
if($taskOldHash -ne 'B4F80DA9431B77386EAE2C2FEAF10C828EEC861E7B6CAC2E5F02387EEC1217D8'){throw 'Previous installer hash mismatch'}
$taskOldInstall=Start-Process -FilePath $taskOldSetup -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -PassThru -Wait
if($taskOldInstall.ExitCode -ne 0){throw 'Previous installer failed'}
$taskOldDll=Join-Path $env:ProgramFiles 'Yomitsugu/0.2.9-preview/ime_mixed_tip_v13.dll'
if((Get-Item -LiteralPath $taskClsid).GetValue('') -ne $taskOldDll){throw 'Previous registration missing'}
$taskProfile=Join-Path $env:LOCALAPPDATA 'ImeMixed'
[IO.Directory]::CreateDirectory($taskProfile) | Out-Null
$taskUtf8=New-Object Text.UTF8Encoding($false)
$taskFixtures=@{
    'user_dictionary.tsv'="こうしんしけん`t更新試験`t名詞`n"
    'settings.json'='{"learning_enabled":true}'
    'learning.json'='{"version":1,"entries":[{"key":"こうしん","text":"更新","count":2,"used":1}]}'
}
$taskProfileHashes=@{}
foreach($taskName in $taskFixtures.Keys){
    $taskFile=Join-Path $taskProfile $taskName
    [IO.File]::WriteAllText($taskFile,$taskFixtures[$taskName],$taskUtf8)
    $taskProfileHashes[$taskName]=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash
}
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
foreach($taskName in $taskProfileHashes.Keys){
    if((Get-FileHash -LiteralPath (Join-Path $taskProfile $taskName) -Algorithm SHA256).Hash -ne $taskProfileHashes[$taskName]){throw ('Upgrade changed user data: '+$taskName)}
}
Write-Output 'PASS Updater upgraded 0.2.9 to 0.2.10 and preserved dictionary, settings, and learning'
python (Join-Path $PSScriptRoot 'installed_conversion.py') (Join-Path $taskInstallDir 'engine/ime_engine_host.exe')
if($LASTEXITCODE -ne 0){throw 'Installed engine conversion failed'}
$taskDriver=Join-Path $taskRoot 'native/build/e2e_out_v14/e2e_tip_tests.exe'
$taskResults=@()
foreach($taskCase in @(@{name='full';arguments=@()},@{name='reported';arguments=@('--reported-inputs')},@{name='stability';arguments=@('--stability-inputs')},@{name='learning';arguments=@('--learning-inputs')})){
    $taskLog=Join-Path $taskAudit ($taskCase.name+'.log')
    $taskStart=@{FilePath=$taskDriver;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=$taskLog;RedirectStandardError=(Join-Path $taskAudit ($taskCase.name+'.err'))}
    if($taskCase.arguments.Count){$taskStart.ArgumentList=$taskCase.arguments}
    $taskProcess=Start-Process @taskStart
    $taskHandle=$taskProcess.Handle
    if(-not $taskProcess.WaitForExit(300000)){
        Stop-Process -Id $taskProcess.Id
        Get-Content -LiteralPath $taskLog -Encoding UTF8
        throw ('Input test timed out: '+$taskCase.name)
    }
    $taskLines=Get-Content -LiteralPath $taskLog -Encoding UTF8
    if($taskProcess.ExitCode -ne 0){$taskLines}else{$taskLines | Select-String 'PASS|FAIL|E2E done|TIPDIAG'}
    $taskResults+=@{name=$taskCase.name;exit=$taskProcess.ExitCode;summary=($taskLines | Select-String 'E2E done').Line}
}
@{installer_sha256=$taskHash;registered_dll=$taskRegistered;upgraded_from='0.2.9-preview';profile_preserved=$true;tests=$taskResults} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskAudit 'result.json') -Encoding UTF8
if(@($taskResults | Where-Object {$_.exit -ne 0}).Count){throw 'Installed input E2E failed'}
