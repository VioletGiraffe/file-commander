TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += qtutils file_commander_core imageviewerplugin textviewerplugin qt_app text_encoding_detector

qtutils.subdir = qtutils

file_commander_core.subdir = file-commander-core
file_commander_core.depends = qtutils

imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = file_commander_core

text_encoding_detector.subdir = text-encoding-detector/text-encoding-detector

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils imageviewerplugin textviewerplugin
