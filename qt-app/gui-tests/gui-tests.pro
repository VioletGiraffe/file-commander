TEMPLATE = subdirs

SUBDIRS = combobox
SUBDIRS += qtutils cpputils cpp-template-utils

cpp-template-utils.subdir = ../../cpp-template-utils
cpputils.subdir = ../../cpputils

qtutils.subdir = ../../qtutils
qtutils.depends = cpputils

combobox.depends = qtutils cpputils
