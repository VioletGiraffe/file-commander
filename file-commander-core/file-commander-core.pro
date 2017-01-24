TEMPLATE = lib
TARGET   = core

QT = core widgets gui #gui is required for QFileIconProvider and plugininterface
CONFIG += staticlib
CONFIG += c++14

win*{
	QT += winextras
}

mac* | linux*{
	CONFIG(release, debug|release):CONFIG += Release
	CONFIG(debug, debug|release):CONFIG += Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

HEADERS += \
	src/cfilesystemobject.h \
	src/ccontroller.h \
	src/fileoperationresultcode.h \
	src/cpanel.h \
	src/diskenumerator/cdiskenumerator.h \
	src/iconprovider/ciconprovider.h \
	src/fileoperations/operationcodes.h \
	src/fileoperations/coperationperformer.h \
	src/fileoperations/cfileoperation.h \
	src/shell/cshell.h \
	include/settings.h \
	src/favoritelocationslist/cfavoritelocations.h \
	src/filesystemhelperfunctions.h \
	src/iconprovider/ciconproviderimpl.h \
	src/fasthash.h \
	src/filesearchengine/cfilesearchengine.h

SOURCES += \
	src/cfilesystemobject.cpp \
	src/ccontroller.cpp \
	src/cpanel.cpp \
	src/diskenumerator/cdiskenumerator.cpp \
	src/iconprovider/ciconprovider.cpp \
	src/fileoperations/coperationperformer.cpp \
	src/shell/cshell.cpp \
	src/favoritelocationslist/cfavoritelocations.cpp \
	src/fasthash.c \
	src/filesearchengine/cfilesearchengine.cpp

DEFINES += PLUGIN_MODULE

INCLUDEPATH += \
	src \
	include \
	../qtutils \
	../cpputils \
	../cpp-template-utils

include(src/pluginengine/pluginengine.pri)
include(src/plugininterface/plugininterface.pri)

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX _SCL_SECURE_NO_WARNINGS

	QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

mac* | linux* {
	QMAKE_CFLAGS   += -pedantic-errors -std=c99
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

LIBS += -L$${DESTDIR} -lcpputils -lqtutils

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}
