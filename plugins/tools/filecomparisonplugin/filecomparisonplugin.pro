TEMPLATE = lib
TARGET   = plugin_filecomparison

QT = core gui widgets
CONFIG += strict_c++ c++latest

mac* | linux* | freebsd{
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

contains(QT_ARCH, x86_64) {
	ARCHITECTURE = x64
} else {
	ARCHITECTURE = x86
}

Release{
	OUTPUT_DIR=release/$${ARCHITECTURE}
	OUTPUT_DIR_NOARCH=release
}

Debug{
	OUTPUT_DIR=debug/$${ARCHITECTURE}
	OUTPUT_DIR_NOARCH=debug
}

DESTDIR = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -L$${DESTDIR_NOARCH} -lcore -lqtutils -lcpputils

DEFINES += PLUGIN_MODULE

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
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libqtutils.a $${DESTDIR_NOARCH}/libcpputils.a
}

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../cpputils \
	../../../cpp-template-utils \
	../../../3rdparty \
	$$PWD/src

HEADERS += \
	cfilecomparisonplugin.h

SOURCES += \
	cfilecomparisonplugin.cpp
