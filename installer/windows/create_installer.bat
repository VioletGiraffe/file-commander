REM this script must set QTDIR32 and QTDIR64 paths to the root of corresponding Qt builds. Example:
REM set QTDIR32=k:\Qt\5\5.4\msvc2013_opengl\
REM set QTDIR64=k:\Qt\5\5.4\msvc2013_64_opengl\

call set_qt_paths.bat

SETLOCAL

RMDIR  /S /Q binaries\

REM X86
pushd ..\..\
%QTDIR32%\bin\qmake.exe -tp vc -r
popd

call "%VS120COMNTOOLS%VsDevCmd.bat" x86
msbuild ..\..\file-commander.sln /t:Rebuild /p:Configuration=Release;PlatformToolset=v120_xp

xcopy /R /Y ..\..\bin\FileCommander.exe binaries\32\
xcopy /R /Y ..\..\bin\plugin_*.dll binaries\32\

xcopy /R /Y %QTDIR32%\bin\Qt5Core.dll binaries\32\Qt\
xcopy /R /Y %QTDIR32%\bin\Qt5Gui.dll binaries\32\Qt\
xcopy /R /Y %QTDIR32%\bin\Qt5Widgets.dll binaries\32\Qt\
xcopy /R /Y %QTDIR32%\bin\Qt5WinExtras.dll binaries\32\Qt\

xcopy /R /Y %QTDIR32%\bin\icu*.dll binaries\32\Qt\

xcopy /R /Y %QTDIR32%\plugins\imageformats\qgif.dll binaries\32\Qt\imageformats\
xcopy /R /Y %QTDIR32%\plugins\imageformats\qico.dll binaries\32\Qt\imageformats\
xcopy /R /Y %QTDIR32%\plugins\imageformats\qjpeg.dll binaries\32\Qt\imageformats\
xcopy /R /Y %QTDIR32%\plugins\imageformats\qtiff.dll binaries\32\Qt\imageformats\

xcopy /R /Y %QTDIR32%\plugins\platforms\qwindows.dll binaries\32\Qt\platforms\

xcopy /R /Y %SystemRoot%\SysWOW64\msvcr120.dll binaries\32\msvcr\
xcopy /R /Y %SystemRoot%\SysWOW64\msvcp120.dll binaries\32\msvcr\

ENDLOCAL

SETLOCAL

REM X64
pushd ..\..\
%QTDIR64%\bin\qmake.exe -tp vc -r
popd

call "%VS120COMNTOOLS%VsDevCmd.bat" amd64
msbuild ..\..\file-commander.sln /t:Rebuild /p:Configuration=Release;PlatformToolset=v120_xp

xcopy /R /Y ..\..\bin\FileCommander.exe binaries\64\
xcopy /R /Y ..\..\bin\plugin_*.dll binaries\64\

xcopy /R /Y %QTDIR64%\bin\Qt5Core.dll binaries\64\Qt\
xcopy /R /Y %QTDIR64%\bin\Qt5Gui.dll binaries\64\Qt\
xcopy /R /Y %QTDIR64%\bin\Qt5Widgets.dll binaries\64\Qt\
xcopy /R /Y %QTDIR64%\bin\Qt5WinExtras.dll binaries\64\Qt\

xcopy /R /Y %QTDIR64%\bin\icu*.dll binaries\64\Qt\

xcopy /R /Y %QTDIR64%\plugins\imageformats\qgif.dll binaries\64\Qt\imageformats\
xcopy /R /Y %QTDIR64%\plugins\imageformats\qico.dll binaries\64\Qt\imageformats\
xcopy /R /Y %QTDIR64%\plugins\imageformats\qjpeg.dll binaries\64\Qt\imageformats\
xcopy /R /Y %QTDIR64%\plugins\imageformats\qtiff.dll binaries\64\Qt\imageformats\

xcopy /R /Y %QTDIR64%\plugins\platforms\qwindows.dll binaries\64\Qt\platforms\

xcopy /R /Y %SystemRoot%\System32\msvcr120.dll binaries\64\msvcr\
xcopy /R /Y %SystemRoot%\System32\msvcp120.dll binaries\64\msvcr\

"c:\Program Files (x86)\Inno Setup 5\compil32" /cc setup.iss

ENDLOCAL