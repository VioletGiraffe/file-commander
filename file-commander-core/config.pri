QT = core widgets gui #gui is required for QFileIconProvider and plugininterface

# Don't override app targets with staticlib, only actual libraries
contains(TEMPLATE, ".*lib"): CONFIG += staticlib

include(../global.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

mac* | linux* | freebsd: CONFIG += strict_c c99

DEFINES += PLUGIN_MODULE

INCLUDEPATH += \
	$$PWD/src \
	$$PWD/include \
	$$PWD/../qtutils \
	$$PWD/../cpputils \
	$$PWD/../cpp-template-utils \
	$$PWD/../thin_io/src \
	$$PWD/../file-operations
