TEMPLATE = lib
TARGET   = plugin_imageviewer

QT = core gui widgets

include(../../../global.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

INCLUDEPATH += \
	../../../qtutils \
	../../../cpputils \
	../../../cpp-template-utils \
	../../../image-processing \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L$${DESTDIR} -limage-processing -lqtutils -lcpputils
include(../../../file-commander-core/core.pri)

HEADERS += \
	src/cimageinfodialog.h \
	src/cimageviewerplugin.h \
	src/cimageviewer.h

SOURCES += \
	src/cimageviewerplugin.cpp \
	src/cimageviewer.cpp

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libimage-processing.a $${DESTDIR}/libcpputils.a $${DESTDIR}/libqtutils.a
}
