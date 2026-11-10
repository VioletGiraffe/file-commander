TEMPLATE = subdirs

SUBDIRS = textviewerplugin qutepart imageviewerplugin csvviewerplugin filecomparisonplugin qt_app qtutils text_encoding_detector file_commander_core pluginapi fileoperations fileoperationsui autoupdater cpputils image-processing cpp-template-utils thin_io tvision fcterm

autoupdater.subdir = github-releases-autoupdater

text_encoding_detector.subdir = text-encoding-detector/text-encoding-detector

fileoperations.subdir = file-operations/libraries/fileoperations
fileoperationsui.subdir = file-operations/libraries/fileoperationsui

# Everything linking core also links fileoperations and thin_io: core calls into both.
imageviewerplugin.subdir = plugins/viewer/imageviewer
imageviewerplugin.depends = pluginapi file_commander_core fileoperations image-processing qtutils thin_io cpputils

qutepart.subdir = plugins/viewer/textviewer/qutepart

textviewerplugin.subdir = plugins/viewer/textviewer
textviewerplugin.depends = qutepart pluginapi file_commander_core fileoperations text_encoding_detector qtutils thin_io cpputils

csvviewerplugin.subdir = plugins/viewer/csvviewer
csvviewerplugin.depends = pluginapi file_commander_core fileoperations text_encoding_detector qtutils thin_io cpputils

filecomparisonplugin.subdir = plugins/tools/filecomparisonplugin
filecomparisonplugin.depends = qtutils pluginapi file_commander_core fileoperations thin_io cpputils

file_commander_core.subdir = file-commander-core

pluginapi.subdir = file-commander-core/pluginapi

tvision.subdir = tui/tvision

# The terminal UI
fcterm.subdir = tui/app
fcterm.depends = tvision file_commander_core fileoperations thin_io cpputils

qt_app.subdir  = qt-app
qt_app.depends = pluginapi file_commander_core fileoperations fileoperationsui qtutils imageviewerplugin textviewerplugin csvviewerplugin autoupdater image-processing filecomparisonplugin thin_io cpputils
