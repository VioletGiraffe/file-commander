TEMPLATE = subdirs

SUBDIRS = qt_app qtutils text_encoding_detector file_commander_core autoupdater cpputils image-processing cpp-template-utils thin_io textviewerplugin imageviewerplugin filecomparisonplugin

qtutils.depends = cpputils image-processing

autoupdater.subdir = github-releases-autoupdater
autoupdater.depends = cpputils

text_encoding_detector.subdir = text-encoding-detector/text-encoding-detector
text_encoding_detector.depends = cpputils

image-processing.depends = cpputils

imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core image-processing qtutils

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = file_commander_core text_encoding_detector qtutils

filecomparisonplugin.subdir = plugins/tools/filecomparisonplugin
filecomparisonplugin.depends = qtutils file_commander_core

file_commander_core.subdir = file-commander-core
file_commander_core.depends = qtutils thin_io

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils imageviewerplugin textviewerplugin autoupdater image-processing filecomparisonplugin
