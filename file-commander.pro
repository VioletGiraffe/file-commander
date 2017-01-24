TEMPLATE = subdirs

SUBDIRS += qt_app qtutils text_encoding_detector file_commander_core autoupdater cpputils
SUBDIRS += textviewerplugin cpp-template-utils imageviewerplugin filecomparisonplugin

qtutils.depends = cpputils

autoupdater.subdir = github-releases-autoupdater
autoupdater.depends = cpputils

file_commander_core.subdir = file-commander-core
file_commander_core.depends = qtutils

imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = file_commander_core text_encoding_detector

filecomparisonplugin.subdir = plugins/tools/filecomparisonplugin
filecomparisonplugin.depends = file_commander_core

text_encoding_detector.subdir = text-encoding-detector/text-encoding-detector
text_encoding_detector.depends = cpputils

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils imageviewerplugin textviewerplugin autoupdater
