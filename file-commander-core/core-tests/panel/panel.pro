TEMPLATE = app
TARGET   = panel_test
CONFIG += console

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH  = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lqtutils -ltest_utils
LIBS += -L$${DESTDIR_NOARCH} -lcpputils

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libqtutils.a $${DESTDIR_NOARCH}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

SOURCES += \
	main.cpp \
	pathnavigationtests.cpp \
	refreshnotificationtests.cpp \
	currentitemtests.cpp \
	historytests.cpp \
	contentsaccesstests.cpp \
	lifetimetests.cpp \
	../../src/cpanel.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/filesystemhelperfunctions.cpp \
	../../src/directoryscanner.cpp \
	../../src/filesystemhelpers/filesystemhelpers.cpp \
	../../src/filesystemhelpers/filestatistics.cpp \
	../../src/filesystemwatcher/cfilesystemwatchertimerbased.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp

HEADERS += \
	paneltesthelpers.h \
	../../src/cpanel.h \
	../../src/cfilesystemobject.h \
	../../src/filesystemhelpers/filesystemhelpers.hpp \
	../../src/filesystemhelpers/filestatistics.h \
	../../src/filesystemwatcher/cfilesystemwatchertimerbased.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h

win*{
	SOURCES += ../../src/filesystemwatcher/cfilesystemwatcherwindows.cpp
	HEADERS += ../../src/filesystemwatcher/cfilesystemwatcherwindows.h
}
