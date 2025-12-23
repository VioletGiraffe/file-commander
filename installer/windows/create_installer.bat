:: this script must set QTDIR32 and QTDIR64 paths to the root of corresponding Qt builds. Example:
:: set QTDIR32=k:\Qt\5\5.4\msvc2013_opengl\
:: set QTDIR64=k:\Qt\5\5.4\msvc2013_64_opengl\

call set_qt_paths.bat

RMDIR /S /Q binaries\

SETLOCAL

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
%QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --force --release --no-translations --no-system-d3d-compiler --no-opengl-sw binaries\64\FileCommander.exe
FOR %%p IN (binaries\64\plugin_*.dll) DO %QTDIR64%\bin\windeployqt.exe --dir binaries\64\Qt --force --release --no-translations --no-system-d3d-compiler --no-opengl-sw %%p
ENDLOCAL

pushd binaries\64\Qt\

mkdir ..\vc_redist\
move vc_redist.x64.exe ..\vc_redist\

for %%F in (
    opengl32sw.dll
    d3dcompiler_*.dll
    dx*compiler.dll
    dxil.dll
) do if exist %%F del %%F

popd

"%programfiles(x86)%\Inno Setup 6\iscc" setup.iss

ENDLOCAL
exit /b 0

:build_fail
ENDLOCAL
echo Build failed
pause
exit /b 1
