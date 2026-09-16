TEMPLATE = app
CONFIG += console
TARGET = csvviewer_test

QT = core

include(../../../../global.pri)

# global.pri's LTO removed: only tests with benchmarks use LTO
# MSVC keeps /GL and /LTCG: linking the /GL-built cpputils forces LTCG
QMAKE_CFLAGS   -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_CXXFLAGS -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_LFLAGS   -= -flto=auto -flto=thin

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../../build/$${OUTPUT_DIR}/$${TARGET}

INCLUDEPATH += \
	../src \
	../../../../cpputils \
	../../../../cpp-template-utils

LIBS += -L$${DESTDIR} -lcpputils

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcpputils.a
}

SOURCES += \
	main.cpp \
	csvparser_test.cpp \
	csvtablemodel_test.cpp \
	../src/ccsvparser.cpp \
	../src/ccsvtablemodel.cpp

HEADERS += \
	../src/ccsvparser.h \
	../src/ccsvtablemodel.h
