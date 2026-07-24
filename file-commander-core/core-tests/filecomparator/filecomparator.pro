TEMPLATE = app
CONFIG += console
TARGET = filecomparator_test

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH  = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libcpputils.a $${DESTDIR_NOARCH}/libqtutils.a $${DESTDIR}/libtest_utils.a
}

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

LIBS += -L$${DESTDIR} -L$${DESTDIR_NOARCH} -lcpputils -lqtutils -ltest_utils

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

