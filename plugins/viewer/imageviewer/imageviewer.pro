TEMPLATE = lib
TARGET   = plugin_imageviewer
DESTDIR  = ../../../bin

QT = core gui
CONFIG += c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

OBJECTS_DIR = ../../../build/imageviewer
MOC_DIR     = ../../../build/imageviewer
UI_DIR      = ../../../build/imageviewer
RCC_DIR     = ../../../build/imageviewer

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../plugininterface/src \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L../../../bin -lplugininterface -lcore

win*{
	QMAKE_CXXFLAGS += /MP
	QMAKE_CXXFLAGS_WARN_ON = -W3
}

mac*{

}

linux*{

}

linux*|mac*{
	QMAKE_CXXFLAGS += --std=c++11
}

HEADERS += \
	cimageviewerplugin.h \
	QtIncludes.h

SOURCES += \
	cimageviewerplugin.cpp

win32*:!*msvc-2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}
