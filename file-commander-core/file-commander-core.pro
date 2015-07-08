TEMPLATE = lib
TARGET   = core
DESTDIR  = ../bin

QT = core widgets gui #gui is required for QFileIconProvider and plugininterface
CONFIG += staticlib
CONFIG += c++14

win*{
	QT += winextras
}

OBJECTS_DIR = ../build/core
MOC_DIR     = ../build/core
UI_DIR      = ../build/core
RCC_DIR     = ../build/core

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

SOURCES += \
	src/cfilesystemobject.cpp \
	src/ccontroller.cpp \
	src/cpanel.cpp \
	src/diskenumerator/cdiskenumerator.cpp \
	src/iconprovider/ciconprovider.cpp \
	src/fileoperations/coperationperformer.cpp \
	src/shell/cshell.cpp \
	src/favoritelocationslist/cfavoritelocations.cpp \
	src/fasthash.c

DEFINES += _SCL_SECURE_NO_WARNINGS PLUGIN_MODULE

INCLUDEPATH += \
	src \
	include \
	../qtutils \
	../cpputils

include(src/pluginengine/pluginengine.pri)
include(src/plugininterface/plugininterface.pri)

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
}

mac* | linux* {
	QMAKE_CFLAGS   += -pedantic-errors -std=c99
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register
}

LIBS += -L../bin -lqtutils

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a
}
