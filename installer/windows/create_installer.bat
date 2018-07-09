REM this script must set QTDIR32 and QTDIR64 paths to the root of corresponding Qt builds. Example:
REM set QTDIR32=k:\Qt\5\5.4\msvc2013_opengl\
REM set QTDIR64=k:\Qt\5\5.4\msvc2013_64_opengl\

call set_qt_paths.bat
set VS_TOOLS_DIR=%VS140COMNTOOLS%

SETLOCAL

RMDIR /S /Q binaries\

call "%VS_TOOLS_DIR%..\..\VC\vcvarsall.bat" x86

REM X86
pushd ..\..\
%QTDIR32%\bin\qmake.exe -tp vc -r
popd

msbuild ..\..\file-commander.sln /t:Build /p:Configuration=Release;PlatformToolset=v140;Platform="Win32"

xcopy /R /Y ..\..\bin\release\x86\FileCommander.exe binaries\32\
xcopy /R /Y ..\..\bin\release\x86\plugin_*.dll binaries\32\
xcopy /R /Y "3rdparty binaries"\32\* binaries\32\

SETLOCAL
SET PATH=%QTDIR32%\bin\
%QTDIR32%\bin\windeployqt.exe --dir binaries\32\Qt --force --no-translations --release --no-compiler-runtime --no-angle binaries\32\FileCommander.exe
FOR %%p IN (binaries\32\plugin_*.dll) DO %QTDIR32%\bin\windeployqt.exe --dir binaries\32\Qt --release --no-compiler-runtime --no-angle --no-translations %%p
ENDLOCAL

xcopy /R /Y %SystemRoot%\SysWOW64\msvcp140.dll binaries\32\msvcr\
xcopy /R /Y %SystemRoot%\SysWOW64\vcruntime140.dll binaries\32\msvcr\
xcopy /R /Y "%programfiles(x86)%\Windows Kits\10\Redist\ucrt\DLLs\x86\*" binaries\32\msvcr\

del binaries\32\Qt\opengl*.*

ENDLOCAL

SETLOCAL

call "%VS_TOOLS_DIR%..\..\VC\vcvarsall.bat" amd64

REM X64
pushd ..\..\
%QTDIR64%\bin\qmake.exe -tp vc -r
popd

msbuild ..\..\file-commander.sln /t:Build /p:Configuration=Release;PlatformToolset=v140;Platform="x64"

xcopy /R /Y ..\..\bin\release\x64\FileCommander.exe binaries\64\
xcopy /R /Y ..\..\bin\release\x64\plugin_*.dll binaries\64\
xcopy /R /Y "3rdparty binaries"\64\* binaries\64\

SETLOCAL
SET PATH=%QTDIR64%\bin\
%QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --force --release --no-compiler-runtime --no-angle --no-translations binaries\64\FileCommander.exe
FOR %%p IN (binaries\64\plugin_*.dll) DO %QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --release --no-compiler-runtime --no-angle --no-translations %%p
ENDLOCAL

xcopy /R /Y %SystemRoot%\System32\msvcp140.dll binaries\64\msvcr\
xcopy /R /Y %SystemRoot%\System32\vcruntime140.dll binaries\64\msvcr\
xcopy /R /Y "%programfiles(x86)%\Windows Kits\10\Redist\ucrt\DLLs\x64\*" binaries\64\msvcr\

del binaries\64\Qt\opengl*.*

"c:\Program Files (x86)\Inno Setup 5\iscc" setup.iss

ENDLOCAL