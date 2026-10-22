TEMPLATE = app
TARGET   = fileoperations_gui_test
CONFIG += console

include(../../../file-commander-core/config.pri)

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

LIBS += -L$${DESTDIR} -lqtutils -lcpputils
include(../../../cpp-template-utils/3rdparty/catch2/catch2.pri)

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../../../file-commander-core/src/ \
	../../../file-operations/tests/fileoperationsui/ # fileoperationguitesthelpers.h

# The dialog drives a real job to exact points, so the engine comes with its fault hooks active.
DEFINES += FILE_OPERATIONS_TEST_HOOKS
include(../../../file-operations/libraries/fileoperationsui.pri)
include(../../../file-commander-core/core.pri) # resolveUserPath()

SOURCES += \
	main.cpp \
	fileoperationlaunchtests.cpp \
	fileoperationroutingtests.cpp \
	../../src/progressdialogs/fileoperationlaunch.cpp

HEADERS += \
	../../../file-operations/tests/fileoperationsui/fileoperationguitesthelpers.h \
	../../../file-operations/tests/scriptedplananswers.h \
	../../src/progressdialogs/fileoperationlaunch.h
