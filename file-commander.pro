TEMPLATE = subdirs

SUBDIRS += qt_app file_commander_core qtutils

qtutils.subdir = qtutils

file_commander_core.subdir = file-commander-core
file_commander_core.depends = qtutils

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils
