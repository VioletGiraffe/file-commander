#define AppVerStr GetFileVersion("binaries/FileCommander.exe")

[Setup]
AppName=File Commander
AppId=FileCommander
AppVerName=File Commander {#AppVerStr}
AppPublisher=VioletGiraffe
VersionInfoVersion={#AppVerStr}
VersionInfoTextVersion={#AppVerStr}
AppVersion={#AppVerStr}
DefaultDirName={pf}\File Commander
DefaultGroupName=File Commander
AllowNoIcons=true
LicenseFile=license.rtf
OutputDir=.
OutputBaseFilename=FileCommander
UsePreviousAppDir=yes
;RestartIfNeededByRun=false

SetupIconFile=..\..\qt-app\resources\icon.ico
UninstallDisplayIcon={app}\FileCommander.exe

AppCopyright=VioletGiraffe
WizardImageBackColor=clWhite
ShowTasksTreeLines=yes
;ShowUndisplayableLanguages=yes

;ArchitecturesInstallIn64BitMode=x64

SolidCompression=true
LZMANumBlockThreads=4
Compression=lzma2/ultra64
LZMAUseSeparateProcess=yes
LZMABlockSize=8192

[Tasks]
Name: desktopicon; Description: {cm:CreateDesktopIcon}; GroupDescription: {cm:AdditionalIcons};

[Files]

;Remote binaries
Source: binaries/*; DestDir: {app}; Flags: ignoreversion;

;Qt binaries
Source: binaries/Qt/*; DestDir: {app}; Flags: ignoreversion;

;Qt plugins 
Source: binaries/Qt/imageformats\*; DestDir: {app}\imageformats; Flags: ignoreversion;
Source: binaries/Qt/platforms\*; DestDir: {app}\platforms; Flags: ignoreversion skipifsourcedoesntexist;

;MSVC binaries
Source: binaries/msvcr/*; DestDir: {app}; Flags: ignoreversion;

;License
Source: license.rtf; DestDir: {app}; 

[Icons]
Name: {group}\File Commander; Filename: {app}\FileCommander.exe;
Name: {group}\{cm:UninstallProgram,File Commander}; Filename: {uninstallexe}

Name: {userdesktop}\File Commander; Filename: {app}\FileCommander.exe; Tasks: desktopicon;

[Run]
Filename: {app}\FileCommander.exe; Description: {cm:LaunchProgram,File Commander}; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"