#define AppVersion "0.2.8-preview"
#define PackageRoot AddBackslash(SourcePath) + "..\dist\yomitsugu-0.2.8-preview"
#define OutputRoot AddBackslash(SourcePath) + "..\dist\installer"

[Setup]
AppId=urotsuki-san.Yomitsugu
AppName=Yomitsugu
AppVersion={#AppVersion}
AppVerName=Yomitsugu {#AppVersion}
AppPublisher=urotsuki-san
AppPublisherURL=https://github.com/urotsuki-san/Yomitsugu
AppSupportURL=https://github.com/urotsuki-san/Yomitsugu/issues
DefaultDirName={autopf}\Yomitsugu\{#AppVersion}
DefaultGroupName=Yomitsugu
UsePreviousAppDir=no
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\native\assets\yomitsugu.ico
UninstallDisplayIcon={app}\ime_settings.exe
OutputDir={#OutputRoot}
OutputBaseFilename=Yomitsugu-{#AppVersion}-x64-setup
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
VersionInfoVersion=0.2.8.0
VersionInfoDescription=Yomitsugu preview installer

[Files]
Source: "{#PackageRoot}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; Excludes: "ime_mixed_tip_v12.dll"
Source: "{#PackageRoot}\ime_mixed_tip_v12.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 64bit

[Icons]
Name: "{autoprograms}\Yomitsugu\設定と辞書"; Filename: "{app}\ime_settings.exe"
Name: "{autoprograms}\Yomitsugu\説明と制限事項"; Filename: "{app}\README.md"

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "YomitsuguTray"; ValueData: """{app}\ime_tray.exe"""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\ime_settings.exe"; Description: "設定と辞書を開く"; Flags: postinstall nowait skipifsilent runasoriginaluser
