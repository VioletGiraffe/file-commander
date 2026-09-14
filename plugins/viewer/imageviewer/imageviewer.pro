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
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../cpputils \
	../../../cpp-template-utils \
	../../../image-processing \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L$${DESTDIR} -lcore -limage-processing -lqtutils -lcpputils -lthin_io

HEADERS += \
	src/cimageviewerplugin.h \
	src/cimageviewerwindow.h

SOURCES += \
	src/cimageviewerplugin.cpp \
	src/cimageviewerwindow.cpp

FORMS += \
	src/cimageviewerwindow.ui

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libimage-processing.a $${DESTDIR}/libcpputils.a $${DESTDIR}/libqtutils.a $${DESTDIR}/libthin_io.a
}
