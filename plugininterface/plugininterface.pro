TEMPLATE = lib
TARGET   = plugininterface
DESTDIR  = ../bin

QT = core gui
CONFIG += staticlib c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets


OBJECTS_DIR = ../build
MOC_DIR     = ../build
UI_DIR      = ../build
RCC_DIR     = ../build

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
	src/plugin_export.h

SOURCES += \
	src/cfilecommanderplugin.cpp \
	src/cfilecommanderviewerplugin.cpp
