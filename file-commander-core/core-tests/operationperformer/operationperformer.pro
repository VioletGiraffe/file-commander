TEMPLATE = app
TARGET   = operationperformer_test
CONFIG += console

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NO_ARCH = ../../../bin/$${OUTPUT_DIR_NO_ARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lcpputils -lqtutils -ltest_utils
LIBS += -L$${DESTDIR_NO_ARCH} -lthin_io

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

SOURCES += \
	../../src/filesystemhelperfunctions.cpp \
	operationperformertest.cpp \
	../../src/fileoperations/coperationperformer.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp \
	../../src/directoryscanner.cpp \
	../../src/cfilemanipulator.cpp \
	../../src/filecomparator/cfilecomparator.cpp

HEADERS += \
	../../src/fileoperations/coperationperformer.h \
	../../src/fileoperations/operationcodes.h \
	../../src/cfilesystemobject.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h \
	../../src/directoryscanner.h \
	../../src/cfilemanipulator.h \
	../../src/filecomparator/cfilecomparator.h
