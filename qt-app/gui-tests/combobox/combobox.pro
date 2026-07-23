TEMPLATE = app
TARGET   = combobox_test
QT = core gui widgets

include(../../../global.pri)

contains(QT_ARCH, x86_64) {
	ARCHITECTURE = x64
} else {
	ARCHITECTURE = x86
}

android {
	Release:OUTPUT_DIR=android/release
	Debug:OUTPUT_DIR=android/debug

} else:ios {
	Release:OUTPUT_DIR=ios/release
	Debug:OUTPUT_DIR=ios/debug

} else {
	Release:OUTPUT_DIR=release/$${ARCHITECTURE}
	Debug:OUTPUT_DIR=debug/$${ARCHITECTURE}
}

Release:OUTPUT_DIR_NOARCH=release
Debug:OUTPUT_DIR_NOARCH=debug

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

# cpputils and qtutils are built into the arch-less bin directory, same as for the fileoperations gui-test.
LIBS += -L$${DESTDIR} -L../../../bin/$${OUTPUT_DIR_NOARCH} -lcpputils -lqtutils

INCLUDEPATH += \
	$$PWD/src/ \
	../../../qtutils \
	../../../cpp-template-utils \
	../../../cpputils

DEFINES += _SCL_SECURE_NO_WARNINGS

win*{
	LIBS += -lole32 -lShell32 -lUser32
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = /W4

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

FORMS += \
	src/mainwindow.ui

HEADERS += \
	src/mainwindow.h

SOURCES += \
	src/mainwindow.cpp \
	src/main.cpp
