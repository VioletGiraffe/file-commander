TEMPLATE = subdirs

SUBDIRS += qtutils file_commander_core imageviewerplugin textviewerplugin qt_app

qtutils.subdir = qtutils

file_commander_core.subdir = file-commander-core
file_commander_core.depends = qtutils

imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = file_commander_core

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils
