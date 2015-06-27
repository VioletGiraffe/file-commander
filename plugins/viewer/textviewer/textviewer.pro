TEMPLATE = lib
TARGET   = plugin_textviewer
DESTDIR  = ../../../bin

QT = core gui

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

win*{
	QT += winextras
}

OBJECTS_DIR = ../../../build/textviewer
MOC_DIR     = ../../../build/textviewer
UI_DIR      = ../../../build/textviewer
RCC_DIR     = ../../../build/textviewer

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../text-encoding-detector/text-encoding-detector/src \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L../../../bin -lcore -lqtutils -ltext_encoding_detector

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
}

mac*{
	CONFIG += c++11
}

linux*|mac*{
	QMAKE_CXXFLAGS += -pedantic-errors -std=c++1y
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register
}

HEADERS += \
	src/ctextviewerplugin.h \
	src/ctextviewerwindow.h \
	src/cfinddialog.h

SOURCES += \
	src/ctextviewerplugin.cpp \
	src/ctextviewerwindow.cpp \
	src/cfinddialog.cpp


win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

FORMS += \
	src/ctextviewerwindow.ui \
	src/cfinddialog.ui

RESOURCES += \
	src/icons.qrc

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a
}
