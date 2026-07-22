TEMPLATE = subdirs

SUBDIRS = fileoperations filesystemobject filesystemobject-high-level filecomparator
SUBDIRS += qtutils cpputils cpp-template-utils test-utils thin_io

# The automated GUI-component tests live with the UI sources but build and run with the test suite.
SUBDIRS += gui-fileoperations
gui-fileoperations.subdir = ../../qt-app/gui-tests/fileoperations
gui-fileoperations.depends = qtutils cpputils

cpp-template-utils.subdir = ../../cpp-template-utils
cpputils.subdir = ../../cpputils
thin_io.subdir = ../../thin_io

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

test-utils.depends = qtutils

fileoperations.depends = test-utils
filesystemobject.depends = qtutils
filesystemobject-high-level.depends = qtutils
filecomparator.depends = cpputils test-utils
