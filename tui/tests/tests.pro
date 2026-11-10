TEMPLATE = app
TARGET   = tui_test
CONFIG += console
CONFIG -= app_bundle

include(../../file-commander-core/config.pri)

# global.pri's LTO removed: only tests with benchmarks use LTO
QMAKE_CFLAGS   -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_CXXFLAGS -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_LFLAGS   -= -flto=auto -flto=thin

# The suite drives the fcterm built beside it
DESTDIR  = ../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../build/$${OUTPUT_DIR}/$${TARGET}

include(../../cpp-template-utils/3rdparty/catch2/catch2.pri)

linux*|freebsd: LIBS += -lutil # forkpty

SOURCES += \
	main.cpp \
	cterminalscreen.cpp \
	cterminalsession.cpp \
	tuitests.cpp

HEADERS += \
	cterminalscreen.h \
	cterminalsession.h
