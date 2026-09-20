@echo off
setlocal enabledelayedexpansion

:: Builds file-commander-core\core-tests and runs every test executable. Exit code: 1 if any suite failed or was
:: not built, 2 when the environment is incomplete.
:: The Qt kit comes from qt_kit.bat (QT_ROOT_DIR); the MSVC environment is set up through vswhere unless cl is
:: already on PATH, as it is on CI.
:: Arguments, all optional, in this order:
::   debug           build and run the debug configuration
::   build           only build; nobuild only runs what is already built, for a CI job that deploys in between
::   all             also run the slow tests that generate large files and trees, skipped by default
::   <suite> [args]  run only the suite whose executable name contains <suite>; the arguments after it go to
::                   that executable, e.g. a Catch2 test spec or --std-seed

set "TESTS=fso_test fso_test_high_level panel_test filesearchengine_test userprograms_test filesystemhelpers_test fileoperations_test filecomparator_test fileoperations_gui_test csvviewer_test"

:: shift renumbers %0 along with the arguments, so the script's own directory has to be taken before the first one
set "SCRIPT_DIR=%~dp0"

set "CONFIG=release"
set "MSBUILD_CONFIG=Release"
if /i "%~1"=="debug" (
	set "CONFIG=debug"
	set "MSBUILD_CONFIG=Debug"
	shift
)

set "BUILD=1"
set "RUN=1"
if /i "%~1"=="build" (
	set "RUN="
	shift
) else if /i "%~1"=="nobuild" (
	set "BUILD="
	shift
)

:: Skipped by default: these generate trees of thousands of files and gigabytes of data, and CI runs them
:: repeatedly with fresh seeds. Every other case in both suites still runs.
set "DEFAULT_ARGS_fileoperations_test=~[executor]~[deleteexecutor]"
set "DEFAULT_ARGS_filecomparator_test=~[CFileComparator]"
if /i "%~1"=="all" (
	set "DEFAULT_ARGS_fileoperations_test="
	set "DEFAULT_ARGS_filecomparator_test="
	shift
)

set "SUITE=%~1"
if defined SUITE shift

set "ARGS="
:collect_args
if not "%~1"=="" (
	set "ARGS=!ARGS! %1"
	shift
	goto :collect_args
)

:: Also for a run: the test executables need the Qt DLLs on PATH
call "%SCRIPT_DIR%qt_kit.bat" || exit /b 2

if not defined BUILD goto :after_build
set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
where cl >nul 2>nul || call :vcvars || exit /b 2

pushd "%SCRIPT_DIR%..\file-commander-core\core-tests" || exit /b 2
"%QT_ROOT_DIR%\bin\qmake.exe" -tp vc -r || goto :fail
:: msbuild, not nmake: it also rebuilds what the compiler command line changed for, such as the Qt include paths
msbuild /t:Build /nologo /m /v:minimal /p:Configuration=%MSBUILD_CONFIG%;PlatformToolset=v143 core-tests.sln || goto :fail
popd
:after_build
if not defined RUN exit /b 0

set "BIN=%SCRIPT_DIR%..\bin\%CONFIG%"
set "FAILED="
set "RAN="
for %%t in (%TESTS%) do call :run_suite %%t
if not defined RAN (
	echo No test executable matches "%SUITE%".
	exit /b 2
)
if defined FAILED (
	echo.
	echo FAILED:%FAILED%
	exit /b 1
)
echo.
echo All suites passed:%RAN%
exit /b 0

:run_suite
set "NAME=%~1"
:: Removing the suite name leaves the whole name unchanged exactly when it does not occur in it
if defined SUITE if "!NAME:%SUITE%=!"=="%NAME%" exit /b 0
set "RAN=!RAN! %~1"
if not exist "%BIN%\%~1.exe" (
	echo %~1: not built
	set "FAILED=!FAILED! %~1"
	exit /b 0
)
set "SUITE_ARGS=%ARGS%"
if not defined SUITE_ARGS set "SUITE_ARGS=!DEFAULT_ARGS_%~1!"
echo.
echo ===== %~1
"%BIN%\%~1.exe" %SUITE_ARGS%
if errorlevel 1 set "FAILED=!FAILED! %~1"
exit /b 0

:fail
popd
exit /b 1

:vcvars
for /f "usebackq delims=" %%p in (`"%VSINSTALLER%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%p"
if not defined VSPATH (
	echo No Visual Studio with the C++ toolset was found.
	exit /b 1
)
:: vcvars64.bat itself calls a bare vswhere.exe, which it expects on PATH
set "PATH=%PATH%;%VSINSTALLER%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
