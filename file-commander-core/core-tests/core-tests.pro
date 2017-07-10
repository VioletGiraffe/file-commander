TEMPLATE = subdirs

SUBDIRS = operationperformer
SUBDIRS += qtutils cpputils cpp-template-utils test-utils

cpp-template-utils.subdir = ../../cpp-template-utils
cpputils.subdir = ../../cpputils

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

autoupdater.subdir = ../../github-releases-autoupdater
autoupdater.depends = cpputils

file_commander_core.subdir = ../../file-commander-core
file_commander_core.depends = qtutils

imageviewerplugin.subdir = ../plugins/viewer/imageviewer
imageviewerplugin.depends = file_commander_core

textviewerplugin.subdir = ../plugins/viewer/textviewer
textviewerplugin.depends = file_commander_core text_encoding_detector

filecomparisonplugin.subdir = ../plugins/tools/filecomparisonplugin
filecomparisonplugin.depends = file_commander_core

test-utils.depends = qtutils

operationperformer.depends = test-utils file_commander_core
