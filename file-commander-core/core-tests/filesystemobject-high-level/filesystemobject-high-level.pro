TEMPLATE = app
CONFIG += console
TARGET = fso_test_high_level

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}
INCLUDEPATH += \
	$${PWD}/

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

SOURCES += \
	fso_test_high_level.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/fasthash.c \
	../../src/iconprovider/ciconprovider.cpp

HEADERS += \
	../../src/cfilesystemobject.h \
	../../src/fasthash.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h
