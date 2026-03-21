; Inno Setup script for myterm
; Produces a single-file installer: myterm-VERSION-windows-x64-setup.exe

#define MyAppName "myterm"
#define MyAppPublisher "myterm"
#define MyAppExeName "myterm.exe"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

[Setup]
AppId={{E8A3B5C1-4F2D-4A9E-B6C7-8D1E2F3A4B5C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputBaseFilename=myterm-{#MyAppVersion}-windows-x64-setup
OutputDir=.
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addtopath"; Description: "Add to PATH"; GroupDescription: "Other:"

[Files]
Source: "{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  Path: string;
  AppDir: string;
begin
  if CurStep = ssPostInstall then
  begin
    if IsTaskSelected('addtopath') then
    begin
      AppDir := ExpandConstant('{app}');
      if RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
      begin
        if Pos(Uppercase(AppDir), Uppercase(Path)) = 0 then
        begin
          Path := Path + ';' + AppDir;
          RegWriteStringValue(HKCU, 'Environment', 'Path', Path);
        end;
      end
      else
        RegWriteStringValue(HKCU, 'Environment', 'Path', AppDir);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path: string;
  AppDir: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    AppDir := ExpandConstant('{app}');
    if RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
    begin
      P := Pos(';' + Uppercase(AppDir), Uppercase(Path));
      if P > 0 then
      begin
        Delete(Path, P, Length(AppDir) + 1);
        RegWriteStringValue(HKCU, 'Environment', 'Path', Path);
      end
      else
      begin
        P := Pos(Uppercase(AppDir) + ';', Uppercase(Path));
        if P > 0 then
        begin
          Delete(Path, P, Length(AppDir) + 1);
          RegWriteStringValue(HKCU, 'Environment', 'Path', Path);
        end
        else if Uppercase(Path) = Uppercase(AppDir) then
          RegDeleteValue(HKCU, 'Environment', 'Path');
      end;
    end;
  end;
end;
