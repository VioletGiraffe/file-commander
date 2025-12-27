QT = core widgets gui #gui is required for QFileIconProvider and plugininterface
lessThan(QT_MAJOR_VERSION, 6) {
	win*:QT += winextras
}

CONFIG += staticlib
CONFIG += strict_c++

include(../global.pri)

mac* | linux*|freebsd{
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

win*{
	QMAKE_CXXFLAGS += /MP /Zi /wd4251 /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX _SCL_SECURE_NO_WARNINGS

	QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF

	*msvc* {
		QMAKE_CXXFLAGS += /FS
	}
}

mac* | linux* | freebsd {
	QMAKE_CFLAGS   += -pedantic-errors -std=c99
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

DEFINES += PLUGIN_MODULE

INCLUDEPATH += \
	src \
	include \
	../qtutils \
	../cpputils \
	../cpp-template-utils \
	../thin_io/src \
	../3rdparty
