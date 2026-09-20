@echo off

:: Resolves QT_ROOT_DIR - the Qt kit, the directory holding bin\qmake.exe - into the caller's environment, and
:: puts its bin on PATH so the Qt DLLs are found at run time. In order: QT_ROOT_DIR as already set (what
:: jurplel/install-qt-action exports on CI); local-env.bat beside this script, git-ignored, where a developer
:: sets it for their machine; the default installation location C:\Qt\<version>\msvc*_64, nothing further.
:: Exit code 2 when there is no kit. No setlocal: the caller keeps the result.

if not defined QT_ROOT_DIR if exist "%~dp0local-env.bat" call "%~dp0local-env.bat"
if not defined QT_ROOT_DIR (
	for /d %%v in ("C:\Qt\6.*") do for /d %%k in ("%%~v\msvc*_64") do call :consider_kit "%%~nxv" "%%~k"
	for %%x in (BEST_VERSION MAJOR MINOR PATCH VERSION) do set "%%x="
)
if not defined QT_ROOT_DIR (
	echo QT_ROOT_DIR is not set and no kit was found under C:\Qt. Set it to the Qt kit directory, the one holding bin\qmake.exe.
	exit /b 2
)
if not exist "%QT_ROOT_DIR%\bin\qmake.exe" (
	echo No qmake.exe under "%QT_ROOT_DIR%\bin".
	exit /b 2
)
set "PATH=%QT_ROOT_DIR%\bin;%PATH%"
exit /b 0

:: Keeps the highest version seen, compared as a zero-padded number per component: plain text order puts 6.9
:: above 6.10. %1 is the version directory's name, %2 the kit under it.
:consider_kit
for /f "tokens=1,2,3 delims=." %%a in ("%~1") do (
	set "MAJOR=00%%a"
	set "MINOR=00%%b"
	set "PATCH=00%%c"
)
set "VERSION=%MAJOR:~-3%%MINOR:~-3%%PATCH:~-3%"
if defined BEST_VERSION if not "%VERSION%" GTR "%BEST_VERSION%" exit /b 0
set "BEST_VERSION=%VERSION%"
set "QT_ROOT_DIR=%~2"
exit /b 0
