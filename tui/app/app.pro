TEMPLATE = app
TARGET   = fcterm

QT = core

CONFIG += console
CONFIG -= app_bundle

include(../../global.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR     = ../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../build/$${OUTPUT_DIR}/$${TARGET}

INCLUDEPATH += \
	$$PWD/src \
	$$PWD/../../tvision/include

SOURCES += \
	src/main.cpp \
	src/ctuiapplication.cpp \
	src/ctuidialogs.cpp \
	src/ctuifilelist.cpp \
	src/ctuifileoperations.cpp \
	src/ctuipanel.cpp

HEADERS += \
	src/ctuiapplication.h \
	src/ctuidialogs.h \
	src/ctuifilelist.h \
	src/ctuifileoperations.h \
	src/ctuipanel.h

LIBS += -L$${DESTDIR} -ltvision
mac*|linux*|freebsd: PRE_TARGETDEPS += $${DESTDIR}/libtvision.a

linux*|freebsd {
	CONFIG += link_pkgconfig
	PKGCONFIG += ncursesw
}
mac*: LIBS += -lncurses

include(../../file-commander-core/core.pri)
