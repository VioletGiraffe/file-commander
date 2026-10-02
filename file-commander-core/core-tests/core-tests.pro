TEMPLATE = subdirs

SUBDIRS = fileoperations filesystemobject filesystemobject-high-level filecomparator panel filesearchengine userprograms filesystemhelpers shell
SUBDIRS += qtutils cpputils cpp-template-utils catch2-runner test-utils thin_io

# The automated GUI-component tests live with the UI sources but build and run with the test suite.
SUBDIRS += gui-fileoperations
gui-fileoperations.subdir = ../../qt-app/gui-tests/fileoperations
gui-fileoperations.depends = qtutils cpputils thin_io catch2-runner

SUBDIRS += gui-filelist
gui-filelist.subdir = ../../qt-app/gui-tests/filelist
gui-filelist.depends = qtutils cpputils thin_io catch2-runner

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

# Every one of these compiles filesystemhelperfunctions.cpp or the file-operation module, both of which call thin_io.
fileoperations.depends = test-utils thin_io catch2-runner
filesystemobject.depends = qtutils thin_io catch2-runner
filesystemobject-high-level.depends = qtutils thin_io catch2-runner
filecomparator.depends = cpputils test-utils thin_io catch2-runner
panel.depends = cpputils test-utils thin_io catch2-runner
filesearchengine.depends = cpputils test-utils thin_io catch2-runner
userprograms.depends = qtutils thin_io catch2-runner
filesystemhelpers.depends = qtutils thin_io catch2-runner
shell.depends = qtutils thin_io catch2-runner
listing-benchmark.depends = qtutils thin_io
filelist-hashmap-benchmark.depends = qtutils thin_io
