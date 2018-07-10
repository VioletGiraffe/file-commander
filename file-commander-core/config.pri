QT = core widgets gui #gui is required for QFileIconProvider and plugininterface
win*:QT += winextras

CONFIG += staticlib

CONFIG += strict_c++ c++14

mac* | linux*{
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

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX _SCL_SECURE_NO_WARNINGS

	QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF

	*msvc* {
		QMAKE_CXXFLAGS += /FS
	}
}

mac* | linux* {
	QMAKE_CFLAGS   += -pedantic-errors -std=c99
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

DEFINES += PLUGIN_MODULE

INCLUDEPATH += \
	src \
	include \
	../qtutils \
	../cpputils \
	../cpp-template-utils
