; myterm Inno Setup installer script
;
; Produces a standard Windows installer:
;   - Installs to Program Files\myterm
;   - Creates Start Menu shortcut
;   - Creates Desktop shortcut (optional)
;   - Registers uninstaller
;   - Optionally adds to PATH
;
; Build with: iscc installer/myterm.iss
; Requires:   Inno Setup 6+ (https://jrsoftware.org/isinfo.php)

#define MyAppName "myterm"
#define MyAppPublisher "myterm contributors"
#define MyAppURL "https://github.com/aacnsilva/myterm"
#define MyAppExeName "myterm.exe"

; Version is passed from CI via /DMyAppVersion=x.y.z
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

[Setup]
AppId={{8F3E4A2B-1C5D-4E6F-9A0B-7D8C3E2F1A4B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=myterm-{#MyAppVersion}-setup-x64
; SetupIconFile=myterm.ico  ; Uncomment when icon file is available
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
MinVersion=10.0
ChangesEnvironment=yes
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addtopath"; Description: "Add myterm to PATH"; GroupDescription: "System integration:"

[Files]
Source: "..\build\myterm.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// Add/remove from user PATH
procedure AddToPath(const Dir: string);
var
  Path: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Path) then
    Path := '';
  if Pos(Uppercase(Dir), Uppercase(Path)) = 0 then
  begin
    if Path <> '' then
      Path := Path + ';';
    Path := Path + Dir;
    RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Path);
  end;
end;

procedure RemoveFromPath(const Dir: string);
var
  Path, UpperDir: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Path) then
    Exit;
  UpperDir := Uppercase(Dir);
  P := Pos(UpperDir, Uppercase(Path));
  if P > 0 then
  begin
    Delete(Path, P, Length(Dir));
    // Clean up stray semicolons
    while Pos(';;', Path) > 0 do
      StringChangeEx(Path, ';;', ';', True);
    if (Length(Path) > 0) and (Path[1] = ';') then
      Delete(Path, 1, 1);
    if (Length(Path) > 0) and (Path[Length(Path)] = ';') then
      Delete(Path, Length(Path), 1);
    RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Path);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if IsTaskSelected('addtopath') then
      AddToPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
