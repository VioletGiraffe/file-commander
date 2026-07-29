TEMPLATE = subdirs

SUBDIRS = fileoperations filesystemobject filesystemobject-high-level filecomparator panel filesearchengine
SUBDIRS += qtutils cpputils cpp-template-utils test-utils thin_io

# The automated GUI-component tests live with the UI sources but build and run with the test suite.
SUBDIRS += gui-fileoperations
gui-fileoperations.subdir = ../../qt-app/gui-tests/fileoperations
gui-fileoperations.depends = qtutils cpputils thin_io

cpp-template-utils.subdir = ../../cpp-template-utils
cpputils.subdir = ../../cpputils
thin_io.subdir = ../../thin_io

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

test-utils.depends = qtutils

# Every one of these compiles filesystemhelperfunctions.cpp or the file-operation module, both of which call thin_io.
fileoperations.depends = test-utils thin_io
filesystemobject.depends = qtutils thin_io
filesystemobject-high-level.depends = qtutils thin_io
filecomparator.depends = cpputils test-utils thin_io
panel.depends = cpputils test-utils thin_io
filesearchengine.depends = cpputils test-utils thin_io
