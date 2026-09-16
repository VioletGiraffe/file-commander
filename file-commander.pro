TEMPLATE = subdirs

SUBDIRS = textviewerplugin qutepart imageviewerplugin csvviewerplugin filecomparisonplugin qt_app qtutils text_encoding_detector file_commander_core autoupdater cpputils image-processing cpp-template-utils thin_io

autoupdater.subdir = github-releases-autoupdater

text_encoding_detector.subdir = text-encoding-detector/text-encoding-detector

# Everything linking core also links thin_io: core's filesystem helpers and its file-operation module call into it.
imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core image-processing qtutils thin_io cpputils

qutepart.subdir = plugins/viewer/textviewer/qutepart

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = qutepart file_commander_core text_encoding_detector qtutils thin_io cpputils

csvviewerplugin.subdir = plugins/viewer/csvviewer
csvviewerplugin.depends = file_commander_core text_encoding_detector qtutils thin_io cpputils

filecomparisonplugin.subdir = plugins/tools/filecomparisonplugin
filecomparisonplugin.depends = qtutils file_commander_core thin_io cpputils

file_commander_core.subdir = file-commander-core

qt_app.subdir  = qt-app
qt_app.depends = file_commander_core qtutils imageviewerplugin textviewerplugin csvviewerplugin autoupdater image-processing filecomparisonplugin thin_io cpputils
