param([switch]$DeveloperUnsigned)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run the 64-bit Windows PowerShell.' }
$taskAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $taskAdmin) { throw 'Open Windows PowerShell as Administrator and run this script once.' }
$taskPackage = $PSScriptRoot
$taskManifest = Get-Content -LiteralPath (Join-Path $taskPackage 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $DeveloperUnsigned -and -not $taskManifest.public_release_ready) { throw 'This package has unresolved release gates. Use only for developer evaluation with -DeveloperUnsigned.' }
foreach ($taskEntry in $taskManifest.files) {
    $taskFile = [IO.Path]::GetFullPath((Join-Path $taskPackage $taskEntry.path))
    if (-not $taskFile.StartsWith($taskPackage + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid manifest path' }
    if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash -ne $taskEntry.sha256) { throw "Hash mismatch: $($taskEntry.path)" }
    if (-not $DeveloperUnsigned -and ([IO.Path]::GetExtension($taskFile) -in @('.dll','.exe','.ps1'))) {
        if ((Get-AuthenticodeSignature -LiteralPath $taskFile).Status -ne 'Valid') { throw "Signature missing/invalid: $($taskEntry.path)" }
    }
}
$taskInstallRoot = Join-Path $env:ProgramFiles 'ImeMixed'
$taskTarget = Join-Path $taskInstallRoot '0.2.10-preview'
if (Test-Path -LiteralPath $taskTarget) { throw 'Version already installed. Unregister it and use a new version directory; do not overwrite a loaded TIP.' }
$taskClsidKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}\InProcServer32'
$taskPrevious = $null
if (Test-Path -LiteralPath $taskClsidKey) { $taskPrevious = (Get-Item -LiteralPath $taskClsidKey).GetValue('') }
New-Item -ItemType Directory -Path $taskInstallRoot -Force | Out-Null
Copy-Item -LiteralPath $taskPackage -Destination $taskTarget -Recurse
$taskDll = Join-Path $taskTarget 'ime_mixed_tip_v14.dll'
$taskReg = Start-Process -FilePath "$env:SystemRoot/System32/regsvr32.exe" -ArgumentList '/s',"`"$taskDll`"" -WindowStyle Hidden -Wait -PassThru
if ($taskReg.ExitCode -ne 0) {
    if ($taskPrevious -and (Test-Path -LiteralPath $taskPrevious)) {
        $taskRollback = Start-Process -FilePath "$env:SystemRoot/System32/regsvr32.exe" -ArgumentList '/s',"`"$taskPrevious`"" -WindowStyle Hidden -Wait -PassThru
        Write-Output "Previous registration restored: exit=$($taskRollback.ExitCode)"
    }
    throw "Registration failed: $($taskReg.ExitCode). Files retained for diagnosis."
}
$taskTray = Join-Path $taskTarget 'ime_tray.exe'
$taskRunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -Path $taskRunKey -Force | Out-Null
New-ItemProperty -Path $taskRunKey -Name 'ImeMixedTray' -Value ('"' + $taskTray + '"') -PropertyType String -Force | Out-Null
$taskMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Yomitsugu'
New-Item -ItemType Directory -Path $taskMenu -Force | Out-Null
$taskShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $taskMenu '設定と辞書.lnk'))
$taskShortcut.TargetPath = Join-Path $taskTarget 'ime_settings.exe'
$taskShortcut.WorkingDirectory = $taskTarget
$taskShortcut.IconLocation = (Join-Path $taskTarget 'ime_settings.exe') + ',0'
$taskShortcut.Save()
Write-Output 'Installed for evaluation. The settings shortcut and per-user tray startup were added. The default IME was not changed.'
