TEMPLATE = app
TARGET   = operationperformer_test

include(../../config.pri)

QT += testlib

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lcpputils -lqtutils -ltest_utils

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

SOURCES += \
	operationperformertest.cpp \
	../../src/fileoperations/coperationperformer.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp \
	../../src/fasthash.c \
	../../src/directoryscanner.cpp \
	../../src/cfilemanipulator.cpp

HEADERS += \
	../../src/fileoperations/cfileoperation.h \
	../../src/fileoperations/coperationperformer.h \
	../../src/fileoperations/operationcodes.h \
	../../src/cfilesystemobject.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h \
	../../src/fasthash.h \
	../../src/directoryscanner.h \
	../../src/cfilemanipulator.h
