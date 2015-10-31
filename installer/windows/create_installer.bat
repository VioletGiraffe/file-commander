REM this script must set QTDIR32 and QTDIR64 paths to the root of corresponding Qt builds. Example:
REM set QTDIR32=k:\Qt\5\5.4\msvc2013_opengl\
REM set QTDIR64=k:\Qt\5\5.4\msvc2013_64_opengl\

call set_qt_paths.bat
set VS_TOOLS_DIR=%VS120COMNTOOLS%

SETLOCAL

RMDIR /S /Q binaries\

REM X86
pushd ..\..\
%QTDIR32%\bin\qmake.exe -tp vc -r
popd

call "%VS_TOOLS_DIR%VsDevCmd.bat" x86
msbuild ..\..\file-commander.sln /t:Rebuild /p:Configuration=Release;PlatformToolset=v120_xp

xcopy /R /Y ..\..\bin\release\FileCommander.exe binaries\32\
xcopy /R /Y ..\..\bin\release\plugin_*.dll binaries\32\

SETLOCAL
SET PATH=%QTDIR32%\bin\
%QTDIR32%\bin\windeployqt.exe --dir binaries\32\Qt --force --no-translations --release --no-compiler-runtime --no-angle binaries\32\FileCommander.exe
FOR %%p IN (binaries\32\plugin_*.dll) DO %QTDIR32%\bin\windeployqt.exe --dir binaries\32\Qt --release --no-compiler-runtime --no-angle --no-translations %%p
ENDLOCAL

xcopy /R /Y %SystemRoot%\SysWOW64\msvcr120.dll binaries\32\msvcr\
xcopy /R /Y %SystemRoot%\SysWOW64\msvcp120.dll binaries\32\msvcr\

del binaries\32\Qt\opengl*.*

ENDLOCAL

SETLOCAL

REM X64
pushd ..\..\
%QTDIR64%\bin\qmake.exe -tp vc -r
popd

call "%VS_TOOLS_DIR%VsDevCmd.bat" amd64
msbuild ..\..\file-commander.sln /t:Rebuild /p:Configuration=Release;PlatformToolset=v120_xp

xcopy /R /Y ..\..\bin\release\FileCommander.exe binaries\64\
xcopy /R /Y ..\..\bin\release\plugin_*.dll binaries\64\

SETLOCAL
SET PATH=%QTDIR64%\bin\
%QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --force --release --no-compiler-runtime --no-angle --no-translations binaries\64\FileCommander.exe
FOR %%p IN (binaries\64\plugin_*.dll) DO %QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --release --no-compiler-runtime --no-angle --no-translations %%p
ENDLOCAL

xcopy /R /Y %SystemRoot%\System32\msvcr120.dll binaries\64\msvcr\
xcopy /R /Y %SystemRoot%\System32\msvcp120.dll binaries\64\msvcr\

del binaries\64\Qt\opengl*.*

"c:\Program Files (x86)\Inno Setup 5\iscc" setup.iss

ENDLOCAL