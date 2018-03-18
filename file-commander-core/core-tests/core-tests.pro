TEMPLATE = subdirs

SUBDIRS = operationperformer filesystemobject
SUBDIRS += qtutils cpputils cpp-template-utils test-utils

cpp-template-utils.subdir = ../../cpp-template-utils
cpputils.subdir = ../../cpputils

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

test-utils.depends = qtutils

operationperformer.depends = test-utils
filesystemobject.depends = qtutils
