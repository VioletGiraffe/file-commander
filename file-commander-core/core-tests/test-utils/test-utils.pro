TEMPLATE = lib
CONFIG += staticlib
TARGET   = test_utils

QT = core

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH  = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libqtutils.a $${DESTDIR_NOARCH}/libcpputils.a
}

INCLUDEPATH += ../../src/
for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

HEADERS += \
	src/cfolderenumeratorrecursive.h \
	src/ctestfoldergenerator.h \
	src/catch2_utils.hpp \
	src/foldercomparator.h \
	src/qt_helpers.hpp \
	src/crandomdatagenerator.h

SOURCES += \
	src/cfolderenumeratorrecursive.cpp \
	src/ctestfoldergenerator.cpp \
	src/foldercomparator.cpp \
	src/qt_helpers.cpp \
	src/crandomdatagenerator.cpp
