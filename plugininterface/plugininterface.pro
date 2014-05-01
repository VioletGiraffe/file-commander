TEMPLATE = lib
TARGET   = plugininterface
DESTDIR  = ../bin

QT = core gui
CONFIG += staticlib c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets


OBJECTS_DIR = ../build/plugininterface
MOC_DIR     = ../build/plugininterface
UI_DIR      = ../build/plugininterface
RCC_DIR     = ../build/plugininterface

INCLUDEPATH += \
	$$PWD/include \
	../file-commander-core/include/

DEFINES += PLUGIN_MODULE

win*{
	QMAKE_CXXFLAGS += /MP
	QMAKE_CXXFLAGS_WARN_ON = -W4
}
mac*{

}
linux*{

}

linux*|mac*{
	QMAKE_CXXFLAGS += --std=c++11
}

HEADERS += \
	src/cfilecommanderplugin.h \
	src/cfilecommanderviewerplugin.h \
	src/plugin_export.h \
    src/cviewerwindow.h \
    src/QtIncludes.h

SOURCES += \
	src/cfilecommanderplugin.cpp \
	src/cfilecommanderviewerplugin.cpp \
    src/cviewerwindow.cpp

LIBS += -L../bin -lcore

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}
