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

win*{
	LIBS += -lMpr
}

DEFINES += PLUGIN_MODULE

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
