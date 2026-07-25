:: Update the main repo
@echo off
setlocal

set "REMOTE=%~1"
if "%REMOTE%"=="" set "REMOTE=origin"

git remote set-head %REMOTE% -a >nul || exit /b 1

for /f "delims=" %%H in ('git symbolic-ref --short HEAD') do set "CURRENT=%%H"
for /f "tokens=1,* delims=/" %%A in ('git symbolic-ref --short refs/remotes/%REMOTE%/HEAD') do set "DEFAULT=%%B"

if not "%CURRENT%"=="%DEFAULT%" (
	>&2 echo On branch %CURRENT%, expected %DEFAULT%; aborting.
	exit /b 1
)

git pull --ff-only %REMOTE% %DEFAULT%

:: Init the subrepos
git submodule update --init --recursive
git submodule foreach --recursive "git remote set-head origin --auto >/dev/null 2>&1; b=$(git symbolic-ref --short refs/remotes/origin/HEAD 2>/dev/null); case $b in origin/*) git checkout ${b#origin/} ;; *) echo no default branch in $displaypath ;; esac"

:: Update the subrepos
git submodule foreach --recursive "git pull"