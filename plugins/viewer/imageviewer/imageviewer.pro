TEMPLATE = lib
TARGET   = plugin_imageviewer

QT = core gui widgets

CONFIG += strict_c++

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

win*{
	QMAKE_CXXFLAGS += /MP /Zi /wd4251
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

HEADERS += \
	src/cimageviewerplugin.h \
	src/cimageviewerwindow.h

SOURCES += \
	src/cimageviewerplugin.cpp \
	src/cimageviewerwindow.cpp

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

FORMS += \
	src/cimageviewerwindow.ui

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libimage-processing.a $${DESTDIR}/libcpputils.a $${DESTDIR}/libqtutils.a $${DESTDIR}/libthin_io.a
}
