TEMPLATE = subdirs

SUBDIRS = textviewerplugin imageviewerplugin filecomparisonplugin qt_app qtutils text_encoding_detector file_commander_core autoupdater cpputils image-processing cpp-template-utils thin_io

autoupdater.subdir = github-releases-autoupdater

text_encoding_detector.subdir = text-encoding-detector/text-encoding-detector

# Everything linking core also links thin_io: core's filesystem helpers and its file-operation module call into it.
imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core image-processing qtutils thin_io

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = file_commander_core text_encoding_detector qtutils thin_io

filecomparisonplugin.subdir = plugins/tools/filecomparisonplugin
filecomparisonplugin.depends = qtutils file_commander_core thin_io

file_commander_core.subdir = file-commander-core

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils imageviewerplugin textviewerplugin autoupdater image-processing filecomparisonplugin thin_io
