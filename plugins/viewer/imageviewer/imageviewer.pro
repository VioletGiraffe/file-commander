TEMPLATE = lib
TARGET   = plugin_imageviewer

QT = core gui widgets

CONFIG += strict_c++ c++2b

mac* | linux* | freebsd{
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

contains(QT_ARCH, x86_64) {
	ARCHITECTURE = x64
} else {
	ARCHITECTURE = x86
}

Release{
	OUTPUT_DIR=release/$${ARCHITECTURE}
	OUTPUT_DIR_NOARCH=release
}

Debug{
	OUTPUT_DIR=debug/$${ARCHITECTURE}
	OUTPUT_DIR_NOARCH=debug
}

DESTDIR = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH = ../../../bin/$${OUTPUT_DIR_NOARCH}
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
	../../../3rdparty \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L$${DESTDIR} -L$${DESTDIR_NOARCH} -lcore -limage-processing -lqtutils -lcpputils

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
	src/cimageviewerwidget.h \
	src/cimageviewerwindow.h

SOURCES += \
	src/cimageviewerplugin.cpp \
	src/cimageviewerwidget.cpp \
	src/cimageviewerwindow.cpp

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

FORMS += \
	src/cimageviewerwindow.ui

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libimage-processing.a
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libcpputils.a $${DESTDIR_NOARCH}/libqtutils.a
}
