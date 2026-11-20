#define AppName "MeloBox"
#ifndef AppVersion
  #define AppVersion "1.0"
#endif
#define SourceDir "..\dist\MeloBox"
#define OutputDir "..\dist\installer"

#ifnexist SourceDir + "\MeloBox.exe"
  #error "Missing MeloBox.exe in dist\MeloBox. Build and stage Release files before compiling installer."
#endif
#ifnexist SourceDir + "\vod.txt"
  #error "Missing vod.txt in dist\MeloBox. The bundled first-launch site list is required."
#endif
#ifnexist SourceDir + "\libmpv-2.dll"
  #error "Missing libmpv-2.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Core.dll"
  #error "Missing Qt6Core.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Gui.dll"
  #error "Missing Qt6Gui.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Network.dll"
  #error "Missing Qt6Network.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Qml.dll"
  #error "Missing Qt6Qml.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Quick.dll"
  #error "Missing Qt6Quick.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Multimedia.dll"
  #error "Missing Qt6Multimedia.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\Qt6Widgets.dll"
  #error "Missing Qt6Widgets.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\platforms\qwindows.dll"
  #error "Missing platforms\qwindows.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\tls\qschannelbackend.dll"
  #error "Missing tls\qschannelbackend.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\networkinformation\qnetworklistmanager.dll"
  #error "Missing networkinformation\qnetworklistmanager.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\multimedia\windowsmediaplugin.dll"
  #error "Missing multimedia\windowsmediaplugin.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\qml\QtQuick\qmldir"
  #error "Missing QtQuick QML runtime in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\qml\QtQuick\Controls\qmldir"
  #error "Missing Qt Quick Controls runtime in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\brotlicommon.dll"
  #error "Missing brotlicommon.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\brotlidec.dll"
  #error "Missing brotlidec.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\bz2.dll"
  #error "Missing bz2.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\double-conversion.dll"
  #error "Missing double-conversion.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\freetype.dll"
  #error "Missing freetype.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\harfbuzz.dll"
  #error "Missing harfbuzz.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\jpeg62.dll"
  #error "Missing jpeg62.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\libcrypto-3-x64.dll"
  #error "Missing libcrypto-3-x64.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\md4c.dll"
  #error "Missing md4c.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\pcre2-16.dll"
  #error "Missing pcre2-16.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\zlib1.dll"
  #error "Missing zlib1.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif
#ifnexist SourceDir + "\zstd.dll"
  #error "Missing zstd.dll in dist\MeloBox. Run deployment staging before compiling installer."
#endif

[Setup]
AppId={{8D8C4B3B-4B0A-4E0A-9F0C-AF77D649AD72}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=MeloBox
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=MeloBox-Setup-{#AppVersion}
SetupIconFile=..\resources\windows\appicon.ico
UninstallDisplayIcon={app}\MeloBox.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加图标："; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.lib,*.exp,*.pdb,*Tests.exe,gtest*.dll,gmock*.dll,*.log,*.txt,miniPlayer.exe,redist\*,dmghg_key.pem"
; 上一条用 *.txt 排掉了所有文本文件, 首次启动站点清单要单独放行。
Source: "{#SourceDir}\vod.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\MeloBox"; Filename: "{app}\MeloBox.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\MeloBox"; Filename: "{app}\MeloBox.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\MeloBox.exe"; Description: "启动 MeloBox"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 安装目录整个删除: 运行期间生成的日志/缓存/下载/换版本备份 (MeloBox.exe.bak-*)
; 等不在安装器文件清单里, 不显式声明会让整个目录残留在磁盘上。
Type: filesandordirs; Name: "{app}"
; 应用配置/缓存/日志在 %LocalAppData%\MeloBox (api_sites.json、cache、download 等)
Type: filesandordirs; Name: "{localappdata}\MeloBox"
; 旧版本遗留的配置目录与散落配置文件
Type: filesandordirs; Name: "{localappdata}\miniPlayer"
Type: files; Name: "{localappdata}\api_sites.json"

[Code]
// 卸载时同步清理 QSettings 写入 HKCU 的应用设置 (dmghg/discoveredHosts 等)
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\MeloBox');
end;

