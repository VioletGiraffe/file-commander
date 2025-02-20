TEMPLATE = app
CONFIG += console
TARGET = fso_test_high_level

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH  = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libqtutils.a $${DESTDIR_NOARCH}/libcpputils.a
}

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}
INCLUDEPATH += \
	$${PWD}/

LIBS += -L$${DESTDIR} -L$${DESTDIR_NOARCH} -lqtutils -lcpputils

SOURCES += \
	../../src/filesystemhelperfunctions.cpp \
	fso_test_high_level.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp

HEADERS += \
	../../src/cfilesystemobject.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h
