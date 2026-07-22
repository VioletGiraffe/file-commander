# Single source manifest for the replacement file-operation module.
# Included from file-commander-core.pro and from the direct-source test project core-tests/fileoperations,
# so every module source is listed exactly once.
# The frozen old engine (coperationperformer.*, operationcodes.h, cfilemanipulator.*) is deliberately not listed here.

HEADERS += \
	$$PWD/fileoperationtypes.h \
	$$PWD/centrypath.h \
	$$PWD/thiniobridge.h \
	$$PWD/cfilesystemmutator.h \
	$$PWD/cstagedfilecopy.h \
	$$PWD/operationtesthooks.h

SOURCES += \
	$$PWD/centrypath.cpp \
	$$PWD/cfilesystemmutator.cpp \
	$$PWD/cstagedfilecopy.cpp

# The hook implementation exists only in test builds; production builds see just the no-op stub in the header.
contains(DEFINES, FILE_OPERATIONS_TEST_HOOKS): SOURCES += \
	$$PWD/operationtesthooks.cpp
