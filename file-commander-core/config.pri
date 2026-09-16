QT = core widgets gui #gui is required for QFileIconProvider and plugininterface

CONFIG += staticlib

include(../global.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

# This is so that all the tests link to the library automatically; harmless to anyone who doesn't use the libraries
LIBS += -L$${PWD}/bin/$${OUTPUT_DIR} -lthin_io

mac* | linux* | freebsd: CONFIG += strict_c c99

# cfilesearchengine.cpp uses SSE4.1 intrinsics, and gcc refuses to inline them without this. It belongs here
# rather than with the core library target because the test targets compile that source directly too.
linux*|freebsd:!contains(QT_ARCH, arm.*): QMAKE_CXXFLAGS += -msse4.1

DEFINES += PLUGIN_MODULE

INCLUDEPATH += \
	src \
	include \
	../qtutils \
	../cpputils \
	../cpp-template-utils \
	../thin_io/src
