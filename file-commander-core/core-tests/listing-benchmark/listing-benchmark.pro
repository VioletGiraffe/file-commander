TEMPLATE = app
CONFIG += console
TARGET = listing_benchmark

include(../../config.pri)

# Keeps global.pri's LTO: the listing is measured as the application builds it

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
	$${PWD}/ \
	../../../file-operations/test-utils/src/ # Only header-only helpers are used, so no test_utils link dependency

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

include(../../../file-operations/libraries/fileoperations.pri)

SOURCES += \
	../../src/filesystemhelperfunctions.cpp \
	main.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/panellisting.cpp

HEADERS += \
	../../src/cfilesystemobject.h \
	../../src/panellisting.h
