TEMPLATE = app
CONFIG += console
TARGET = filecomparator_test

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
	PRE_TARGETDEPS += $${DESTDIR}/libcpputils.a $${DESTDIR}/libqtutils.a $${DESTDIR}/libtest_utils.a
}

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

LIBS += -L$${DESTDIR} -lcpputils -lqtutils -ltest_utils

SOURCES += \
	filecomparator_test.cpp \
	foldercomparison_test.cpp \
	../../src/filecomparator/filecontentcomparison.cpp \
	../../src/filecomparator/foldercomparison.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/filesystemhelperfunctions.cpp \
	../../src/directoryscanner.cpp

HEADERS += \
	../../src/filecomparator/filecontentcomparison.h \
	../../src/filecomparator/foldercomparison.h \
	../../src/cfilesystemobject.h \
	../../src/directoryscanner.h

