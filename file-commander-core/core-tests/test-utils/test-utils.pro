TEMPLATE = lib
CONFIG += staticlib
TARGET   = test_utils

QT = core

include(../../config.pri)

# global.pri's LTO removed: only tests with benchmarks use LTO
# MSVC keeps /GL: the tests linking this library link with LTCG
QMAKE_CFLAGS   -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_CXXFLAGS -= -flto=auto -ffat-lto-objects -flto=thin

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += ../../src/
for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

HEADERS += \
	src/ctestfoldergenerator.h \
	src/catch2_utils.hpp \
	src/link_helpers.hpp \
	src/qt_helpers.hpp \
	src/crandomdatagenerator.h

SOURCES += \
	src/ctestfoldergenerator.cpp \
	src/qt_helpers.cpp \
	src/crandomdatagenerator.cpp
