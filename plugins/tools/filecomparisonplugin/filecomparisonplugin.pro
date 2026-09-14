TEMPLATE = lib
TARGET   = plugin_filecomparison

QT = core gui widgets

include(../../../global.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lcore -lqtutils -lcpputils -lthin_io

DEFINES += PLUGIN_MODULE

win*{
	QMAKE_CXXFLAGS += /MP /Zi /wd4251
	Debug:QMAKE_CXXFLAGS += /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = -W4

	Debug:QMAKE_LFLAGS += /INCREMENTAL
}

win32*:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a $${DESTDIR}/libthin_io.a
}

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../cpputils \
	../../../cpp-template-utils \
	$$PWD/src

HEADERS += \
	cfilecomparisonplugin.h

SOURCES += \
	cfilecomparisonplugin.cpp
