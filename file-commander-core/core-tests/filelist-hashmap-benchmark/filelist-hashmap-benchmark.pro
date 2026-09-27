TEMPLATE = app
CONFIG += console
TARGET = filelist_hashmap_benchmark

include(../../config.pri)

# Keeps global.pri's LTO: the containers are measured as the application builds them

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
	../../../cpp-template-utils/3rdparty \
	../test-utils/src/ # Only header-only helpers are used, so no test_utils link dependency

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

SOURCES += \
	../../src/filesystemhelperfunctions.cpp \
	main.cpp \
	../../src/cfilesystemobject.cpp

HEADERS += \
	../../src/cfilesystemobject.h \
	../../src/detail/file_list_hashmap.h
