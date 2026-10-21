#define AppVersion GetVersionNumbersString(AddBackslash(SourcePath) + "binaries\64\FileCommander.exe")
#if AppVersion == ""
  #error FileCommander.exe carries no version resource
#endif

[Setup]
AppName=File Commander
AppId=FileCommander
AppVerName=File Commander
AppVersion={#AppVersion}
AppPublisher=VioletGiraffe
DefaultDirName={pf}\File Commander
DefaultGroupName=File Commander
AllowNoIcons=true
OutputDir=.
OutputBaseFilename=FileCommander
UsePreviousAppDir=yes

WizardStyle=modern

SetupIconFile=..\..\qt-app\resources\icon.ico
UninstallDisplayIcon={app}\FileCommander.exe

AppCopyright=VioletGiraffe
ShowTasksTreeLines=yes

ArchitecturesInstallIn64BitMode=x64

SolidCompression=true
LZMANumBlockThreads=4
Compression=lzma2/ultra64
LZMAUseSeparateProcess=yes
LZMABlockSize=8192

DisableReadyPage=yes

[Tasks]
Name: desktopicon; Description: {cm:CreateDesktopIcon}; GroupDescription: {cm:AdditionalIcons};

[Files]

;App binaries
Source: binaries/64/*; DestDir: {app}; Flags: ignoreversion; Check: Is64BitInstallMode

;3rdparty
Source: binaries/64/Qt/*; DestDir: {app}; Flags: recursesubdirs; Check: Is64BitInstallMode
Source: binaries/64/vc_redist/*; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: {group}\File Commander; Filename: {app}\FileCommander.exe;
Name: {group}\{cm:UninstallProgram,File Commander}; Filename: {uninstallexe}

Name: {userdesktop}\File Commander; Filename: {app}\FileCommander.exe; Tasks: desktopicon;

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Visual C++ Runtime..."; Flags: waituntilterminated
Filename: {app}\FileCommander.exe; Description: {cm:LaunchProgram,File Commander}; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Code]
// Installers before 1.0 recorded no DisplayVersion: such an install is removed before installing over it
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  UninstallKey, Uninstaller, InstalledVersion: String;
  ResultCode: Integer;
begin
  Result := '';
  UninstallKey := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1';
  if not RegQueryStringValue(HKLM, UninstallKey, 'UninstallString', Uninstaller) then
    exit;
  if RegQueryStringValue(HKLM, UninstallKey, 'DisplayVersion', InstalledVersion) then
    exit;

  Uninstaller := RemoveQuotes(Uninstaller);
  // A stale key whose uninstaller is gone gets overwritten by this install
  if not FileExists(Uninstaller) then
    exit;

  if not Exec(Uninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    Result := 'Failed to uninstall the previous version of File Commander. Please uninstall it and run Setup again.';
end;