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

;Qt binaries
Source: binaries/64/Qt/*; DestDir: {app}; Flags: ignoreversion recursesubdirs; Check: Is64BitInstallMode

;MSVC binaries
Source: binaries/64/msvcr/*; DestDir: {app}; Flags: ignoreversion; Check: Is64BitInstallMode

[Icons]
Name: {group}\File Commander; Filename: {app}\FileCommander.exe;
Name: {group}\{cm:UninstallProgram,File Commander}; Filename: {uninstallexe}

Name: {userdesktop}\File Commander; Filename: {app}\FileCommander.exe; Tasks: desktopicon;

[Run]
Filename: {app}\FileCommander.exe; Description: {cm:LaunchProgram,File Commander}; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"