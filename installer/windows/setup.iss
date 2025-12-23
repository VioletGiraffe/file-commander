[Setup]
AppName=File Commander
AppId=FileCommander
AppVerName=File Commander
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
Source: binaries/64/Qt/*; DestDir: {app}; Flags: ignoreversion recursesubdirs; Check: Is64BitInstallMode
Source: binaries/64/vc_redist/*; DestDir: "{tmp}"; Flags: deleteafterinstall nocompression

[Icons]
Name: {group}\File Commander; Filename: {app}\FileCommander.exe;
Name: {group}\{cm:UninstallProgram,File Commander}; Filename: {uninstallexe}

Name: {userdesktop}\File Commander; Filename: {app}\FileCommander.exe; Tasks: desktopicon;

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Visual C++ Runtime..."; Flags: waituntilterminated
Filename: {app}\FileCommander.exe; Description: {cm:LaunchProgram,File Commander}; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"