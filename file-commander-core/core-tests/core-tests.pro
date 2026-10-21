TEMPLATE = subdirs

SUBDIRS = fileoperations fileoperationsui foldercomparison filesystemobject filesystemobject-high-level panel filesearchengine userprograms filesystemhelpers shell
SUBDIRS += qtutils cpputils cpp-template-utils catch2-runner test-utils thin_io

# The file-operation suites live with their module.
fileoperations.subdir = ../../file-operations/tests/fileoperations
fileoperationsui.subdir = ../../file-operations/tests/fileoperationsui
foldercomparison.subdir = ../../file-operations/tests/foldercomparison
test-utils.subdir = ../../file-operations/test-utils

SUBDIRS += fileoperations-lib fileoperations-testhooks-lib fileoperationsui-lib
fileoperations-lib.subdir = ../../file-operations/libraries/fileoperations
fileoperations-testhooks-lib.subdir = ../../file-operations/libraries/fileoperations-testhooks
fileoperationsui-lib.subdir = ../../file-operations/libraries/fileoperationsui

SUBDIRS += core
core.file = ../file-commander-core.pro

# The automated GUI-component tests live with the UI sources but build and run with the test suite.
SUBDIRS += gui-fileoperations
gui-fileoperations.subdir = ../../qt-app/gui-tests/fileoperations
gui-fileoperations.depends = core fileoperations-testhooks-lib fileoperationsui-lib qtutils cpputils thin_io catch2-runner

SUBDIRS += gui-filelist
gui-filelist.subdir = ../../qt-app/gui-tests/filelist
gui-filelist.depends = core fileoperations-lib qtutils cpputils thin_io catch2-runner

SUBDIRS += csvviewer
csvviewer.subdir = ../../plugins/viewer/csvviewer/tests
csvviewer.depends = cpputils catch2-runner

# Built with the suites so they cannot rot, but never run by the test scripts: see doc/testing.md, "Listing benchmark" and "File list map benchmark".
SUBDIRS += listing-benchmark filelist-hashmap-benchmark

cpp-template-utils.subdir = ../../cpp-template-utils
catch2-runner.subdir = ../../cpp-template-utils/3rdparty/catch2
cpputils.subdir = ../../cpputils
thin_io.subdir = ../../thin_io

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

test-utils.depends = qtutils

# Every one of these links a file-operation library, and thin_io with it.
fileoperations.depends = fileoperations-testhooks-lib test-utils thin_io catch2-runner
filesystemobject.depends = core fileoperations-lib qtutils thin_io catch2-runner
filesystemobject-high-level.depends = core fileoperations-lib qtutils thin_io catch2-runner
fileoperationsui.depends = fileoperations-testhooks-lib fileoperationsui-lib qtutils cpputils thin_io catch2-runner
foldercomparison.depends = fileoperations-lib cpputils test-utils thin_io catch2-runner
panel.depends = core fileoperations-lib cpputils test-utils thin_io catch2-runner
filesearchengine.depends = core fileoperations-lib cpputils test-utils thin_io catch2-runner
userprograms.depends = core fileoperations-lib qtutils thin_io catch2-runner
filesystemhelpers.depends = core fileoperations-lib qtutils thin_io catch2-runner
shell.depends = core fileoperations-lib qtutils thin_io catch2-runner
listing-benchmark.depends = core fileoperations-lib qtutils thin_io
filelist-hashmap-benchmark.depends = core fileoperations-lib qtutils thin_io
