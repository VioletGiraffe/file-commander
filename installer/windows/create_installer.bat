rem set QTDIR=c:\Development\Qt\4.8.5\
set QTDIR=k:\Qt\5\5.4\msvc2013_opengl\

RMDIR  /S /Q binaries\

xcopy /R /Y ..\..\bin\FileCommander.exe binaries\
xcopy /R /Y ..\..\bin\plugin_*.dll binaries\

xcopy /R /Y %QTDIR%\bin\Qt5Core.dll binaries\Qt\
xcopy /R /Y %QTDIR%\bin\Qt5Gui.dll binaries\Qt\
xcopy /R /Y %QTDIR%\bin\Qt5Widgets.dll binaries\Qt\

xcopy /R /Y %QTDIR%\bin\icudt52.dll binaries\Qt\
xcopy /R /Y %QTDIR%\bin\icuin52.dll binaries\Qt\
xcopy /R /Y %QTDIR%\bin\icuuc52.dll binaries\Qt\

xcopy /R /Y %QTDIR%\plugins\imageformats\qgif.dll binaries\Qt\imageformats\
xcopy /R /Y %QTDIR%\plugins\imageformats\qico.dll binaries\Qt\imageformats\
xcopy /R /Y %QTDIR%\plugins\imageformats\qjpeg.dll binaries\Qt\imageformats\
xcopy /R /Y %QTDIR%\plugins\imageformats\qtiff.dll binaries\Qt\imageformats\

xcopy /R /Y %QTDIR%\plugins\platforms\qwindows.dll binaries\Qt\platforms\

xcopy /R /Y %SystemRoot%\SysWOW64\msvcr120.dll binaries\msvcr\
xcopy /R /Y %SystemRoot%\SysWOW64\msvcp120.dll binaries\msvcr\
xcopy /R /Y %SystemRoot%\SysWOW64\vcomp120.dll binaries\msvcr\

"c:\Program Files (x86)\Inno Setup 5\compil32" /cc setup.iss