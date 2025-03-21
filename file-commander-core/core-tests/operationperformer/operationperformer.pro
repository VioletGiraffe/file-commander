TEMPLATE = app
TARGET   = operationperformer_test
CONFIG += console

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH  = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lqtutils -ltest_utils
LIBS += -L$${DESTDIR_NOARCH} -lcpputils -lthin_io

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libqtutils.a $${DESTDIR_NOARCH}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

DEFINES += OPERATION_PERFORMER_CHUNK_SIZE=1024

SOURCES += \
	operationperformertest.cpp \
	../../src/filesystemhelperfunctions.cpp \
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
