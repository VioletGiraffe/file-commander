TEMPLATE = lib
CONFIG += staticlib
TARGET   = test_utils

QT = core

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += ../../src/
for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

HEADERS += \
	src/cfolderenumeratorrecursive.h \
	src/ctestfoldergenerator.h

SOURCES += \
	src/cfolderenumeratorrecursive.cpp \
	src/ctestfoldergenerator.cpp
