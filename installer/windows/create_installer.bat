:: this script must set QTDIR32 and QTDIR64 paths to the root of corresponding Qt builds. Example:
:: set QTDIR32=k:\Qt\5\5.4\msvc2013_opengl\
:: set QTDIR64=k:\Qt\5\5.4\msvc2013_64_opengl\

call set_qt_paths.bat

RMDIR /S /Q binaries\

SETLOCAL

if exist "%systemroot%\Sysnative\" (
    set SYS64=%systemroot%\Sysnative
) else (
    set SYS64=%systemroot%\System32
)

echo %SYS64%

if exist "%ProgramW6432%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "%ProgramW6432%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
) else (
    if exist "%ProgramW6432%\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvarsall.bat" (
        call "%ProgramW6432%\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvarsall.bat" amd64
    ) else (
        call "%ProgramW6432%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" amd64
    )
)

:: X64
pushd ..\..\
del .qmake.stash
%QTDIR64%\bin\qmake.exe -tp vc -r
popd

msbuild ../../file-commander.sln /t:Build /p:Configuration=Release;Platform="x64";PlatformToolset=v143
if not %errorlevel% == 0 goto build_fail

xcopy /R /Y ..\..\bin\release\x64\FileCommander.exe binaries\64\
xcopy /R /Y ..\..\bin\release\x64\plugin_*.dll binaries\64\
xcopy /R /Y "3rdparty binaries"\64\* binaries\64\

SETLOCAL
SET PATH=%QTDIR64%\bin\
%QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --force --release --release --force --no-system-d3d-compiler --no-compiler-runtime --no-translations binaries\64\FileCommander.exe
FOR %%p IN (binaries\64\plugin_*.dll) DO %QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --release --force --no-system-d3d-compiler --no-compiler-runtime --no-translations %%p
ENDLOCAL

%SYS64%\cmd.exe /c "xcopy /R /Y %SystemRoot%\System32\msvcp140.dll binaries\64\msvcr\"
if not %errorlevel% == 0 goto dll_not_found
%SYS64%\cmd.exe /c "xcopy /R /Y %SystemRoot%\System32\msvcp140_1.dll binaries\64\msvcr\"
if not %errorlevel% == 0 goto dll_not_found
%SYS64%\cmd.exe /c "xcopy /R /Y %SystemRoot%\System32\vcruntime140.dll binaries\64\msvcr\"
if not %errorlevel% == 0 goto dll_not_found
%SYS64%\cmd.exe /c "xcopy /R /Y %SystemRoot%\System32\vcruntime140_1.dll binaries\64\msvcr\"
if not %errorlevel% == 0 goto dll_not_found

if not defined WIN_SDK (
    if exist "%programfiles(x86)%\Windows Kits\10\Redist\10.0.19041.0" (
        set WIN_SDK=10.0.19041.0
    ) else (
        set WIN_SDK=10.0.18362.0
    )
)

xcopy /R /Y "%programfiles(x86)%\Windows Kits\10\Redist\%WIN_SDK%\ucrt\DLLs\x64\*" binaries\64\msvcr\
if %ERRORLEVEL% GEQ 1 goto windows_sdk_not_found

del binaries\64\Qt\opengl*.*

"%programfiles(x86)%\Inno Setup 6\iscc" setup.iss

ENDLOCAL
exit /b 0

:build_fail
ENDLOCAL
echo Build failed
pause
exit /b 1

:dll_not_found
ENDLOCAL
echo VC++ Redistributable DLL not found
pause
exit /b 1

:windows_sdk_not_found
ENDLOCAL
echo Windows SDK not found (required for CRT DLLs)
pause
exit /b 1