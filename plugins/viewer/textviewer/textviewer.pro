TEMPLATE = lib
TARGET   = plugin_textviewer

QT = core gui widgets
greaterThan(QT_MAJOR_VERSION, 5) {
	QT += core5compat
}

CONFIG += strict_c++ c++2a

mac* | linux* | freebsd{
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

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

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

DEFINES += PLUGIN_MODULE

LIBS += -L../../../bin/$${OUTPUT_DIR} -lcore -lqtutils -ltext_encoding_detector -lcpputils

win*{
	QMAKE_CXXFLAGS += /MP /Zi /wd4251 /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libtext_encoding_detector.a
}

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../cpputils \
	../../../cpp-template-utils \
	../../../text-encoding-detector/text-encoding-detector/src \
	$$PWD/src/

HEADERS += \
	src/ctextviewerplugin.h \
	src/ctextviewerwindow.h \
	src/cfinddialog.h

SOURCES += \
	src/ctextviewerplugin.cpp \
	src/ctextviewerwindow.cpp \
	src/cfinddialog.cpp

FORMS += \
	src/ctextviewerwindow.ui \
	src/cfinddialog.ui

RESOURCES += \
	src/icons.qrc
