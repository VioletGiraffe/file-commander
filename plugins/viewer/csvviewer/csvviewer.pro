TEMPLATE = lib
TARGET   = plugin_csvviewer

QT = core gui widgets core5compat

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
	../../../cpp-template-utils/3rdparty \
	../../../text-encoding-detector/text-encoding-detector/src \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L$${DESTDIR} -lcore -lqtutils -ltext_encoding_detector -lcpputils -lthin_io

HEADERS += \
	src/ccsvparser.h \
	src/ccsvtablemodel.h \
	src/ccsvviewerplugin.h \
	src/ccsvviewerwindow.h

SOURCES += \
	src/ccsvparser.cpp \
	src/ccsvtablemodel.cpp \
	src/ccsvviewerplugin.cpp \
	src/ccsvviewerwindow.cpp

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libtext_encoding_detector.a $${DESTDIR}/libcpputils.a $${DESTDIR}/libqtutils.a $${DESTDIR}/libthin_io.a
}
