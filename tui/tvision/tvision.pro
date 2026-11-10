# The tvision submodule as a static library: the sources, include paths and defines of its source/CMakeLists.txt.
TEMPLATE = lib
TARGET   = tvision

CONFIG += staticlib warn_off
CONFIG -= qt

include(../../global.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR     = ../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../build/$${OUTPUT_DIR}/$${TARGET}

TVISION = $$clean_path($$PWD/../../tvision)

INCLUDEPATH += \
	$$TVISION/include \
	$$TVISION/include/tvision \
	$$TVISION/include/tvision/compat/borland

SOURCES += $$files($$TVISION/source/tvision/*.cpp) $$files($$TVISION/source/platform/*.cpp)
SOURCES -= $$TVISION/source/tvision/geninc.cpp

DEFINES += TVISION_NO_STL

win*: DEFINES += _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0600

!win* {
	INCLUDEPATH += $$TVISION/include/tvision/compat/windows
	DEFINES += HAVE_NCURSES
}

# Supplies malloc.h, which only Windows and Linux have
!win*:!linux*: INCLUDEPATH += $$TVISION/include/tvision/compat/malloc

linux*|freebsd {
	CONFIG += link_pkgconfig
	PKGCONFIG += ncursesw
}
