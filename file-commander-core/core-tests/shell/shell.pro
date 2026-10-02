TEMPLATE = app
CONFIG += console
TARGET   = shell_test

include(../../config.pri)

# global.pri's LTO removed: only tests with benchmarks use LTO
# MSVC keeps /GL and /LTCG: linking the /GL-built qtutils, cpputils and thin_io forces LTCG
QMAKE_CFLAGS   -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_CXXFLAGS -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_LFLAGS   -= -flto=auto -flto=thin

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}
INCLUDEPATH += \
	../../src/

LIBS += -L$${DESTDIR} -lqtutils -lcpputils
include(../../../cpp-template-utils/3rdparty/catch2/catch2.pri)

win*: LIBS += -lole32 -lShell32 -lUser32 -lMpr

SOURCES += \
	main.cpp \
	shelltests.cpp \
	../../src/shell/cshell.cpp \
	../../src/shell/cshellcommand.cpp \
	../../src/filesystemhelperfunctions.cpp

HEADERS += \
	../../src/shell/cshell.h \
	../../src/shell/cshellcommand.h \
	../../src/filesystemhelperfunctions.h
