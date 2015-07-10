TEMPLATE = lib
TARGET   = plugin_imageviewer
DESTDIR  = ../../../bin

QT = core gui

CONFIG += c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

win*{
	QT += winextras
}

OBJECTS_DIR = ../../../build/imageviewer
MOC_DIR     = ../../../build/imageviewer
UI_DIR      = ../../../build/imageviewer
RCC_DIR     = ../../../build/imageviewer

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../cpputils \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L../../../bin -lcore -lqtutils -lcpputils

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
}

linux*{

}

linux*|mac*{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	CONFIG(release, debug|release):CONFIG += Release
	CONFIG(debug, debug|release):CONFIG += Debug

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

HEADERS += \
	src/cimageviewerplugin.h \
	src/cimageviewerwidget.h \
	src/cimageviewerwindow.h

SOURCES += \
	src/cimageviewerplugin.cpp \
	src/cimageviewerwidget.cpp \
	src/cimageviewerwindow.cpp

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

FORMS += \
	src/cimageviewerwidget.ui \
	src/cimageviewerwindow.ui

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a
}
