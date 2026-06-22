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
DefaultDirName={autopf}\BLP Viewer
DefaultGroupName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=BLP_Viewer_Setup_x64
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; 程序的文件关联/设置本就写在当前用户区，卸载清理同理，此警告可忽略
UsedUserAreasWarning=no
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
; 安装时弹出语言选择，默认跟随系统语言
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.LaunchApp=启动 {#AppName}
english.LaunchApp=Launch {#AppName}
chinesesimplified.RegisterThumb=注册资源管理器缩略图（BLP/TGA/PNG）
english.RegisterThumb=Register Explorer thumbnails (BLP/TGA/PNG)
chinesesimplified.UninstallShortcut=卸载 {#AppName}
english.UninstallShortcut=Uninstall {#AppName}

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallShortcut}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
; 默认勾选，安装即创建桌面快捷方式
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "thumbnail"; Description: "{cm:RegisterThumb}"; Flags: unchecked

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent
Filename: "regsvr32.exe"; Parameters: "/s ""{app}\blp_thumbnail.dll"""; Tasks: thumbnail; Flags: runhidden

[UninstallRun]
; 无论当初是否勾选缩略图任务，程序内也可能注册过，统一反注册
Filename: "regsvr32.exe"; Parameters: "/s /u ""{app}\blp_thumbnail.dll"""; RunOnceId: "UnregThumb"; Flags: runhidden

[UninstallDelete]
; 程序设置（%APPDATA%\blp_viewer\settings.ini）
Type: filesandordirs; Name: "{userappdata}\blp_viewer"

[Code]
const
  ViewerProgId = 'BLPViewer.File';
  ThumbClsid = '{E357FCCD-A995-4576-B01F-234630154E96}';

// 程序运行时通过“工具”菜单写入的按用户文件关联，卸载时一并清理。
procedure CleanupAssociation(const Ext: string);
var
  Value: string;
begin
  if RegQueryStringValue(HKCU, 'Software\Classes\' + Ext, '', Value) then
  begin
    if CompareText(Value, ViewerProgId) = 0 then
      RegDeleteValue(HKCU, 'Software\Classes\' + Ext, '');
  end;
  RegDeleteValue(HKCU, 'Software\Classes\' + Ext + '\OpenWithProgids', ViewerProgId);
  RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\' + Ext + '\ShellEx\' + ThumbClsid);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    CleanupAssociation('.blp');
    CleanupAssociation('.png');
    CleanupAssociation('.tga');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\' + ViewerProgId);
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\CLSID\' + ThumbClsid);
  end;
end;
