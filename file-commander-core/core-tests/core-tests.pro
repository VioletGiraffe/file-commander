TEMPLATE = subdirs

SUBDIRS = operationperformer filesystemobject filesystemobject-high-level filecomparator
SUBDIRS += qtutils cpputils cpp-template-utils test-utils thin_io

cpp-template-utils.subdir = ../../cpp-template-utils
cpputils.subdir = ../../cpputils
thin_io.subdir = ../../thin_io

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

test-utils.depends = qtutils

operationperformer.depends = test-utils thin_io
filesystemobject.depends = qtutils
filesystemobject-high-level.depends = qtutils
filecomparator.depends = cpputils test-utils
