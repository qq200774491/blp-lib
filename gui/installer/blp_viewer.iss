#define AppName "图像快速处理工具"
#define AppVersion "1.4.0"
#define AppPublisher "blp-lib"
#define AppExeName "blp_viewer.exe"
#define StageDir "stage"
#define OutputDir "dist"

[Setup]
AppId={{6F7B0C4C-9D72-48B2-9C9A-9E76B06C8D0A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={pf}\BLP Viewer
DefaultGroupName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=BLP_Viewer_Setup_x64
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"

[Tasks]
Name: "thumbnail"; Description: "注册资源管理器缩略图"; Flags: unchecked

[Run]
Filename: "{app}\{#AppExeName}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent
Filename: "regsvr32.exe"; Parameters: "/s \"{app}\blp_thumbnail.dll\""; Tasks: thumbnail; Flags: runhidden

[UninstallRun]
Filename: "regsvr32.exe"; Parameters: "/s /u \"{app}\blp_thumbnail.dll\""; Tasks: thumbnail; Flags: runhidden
