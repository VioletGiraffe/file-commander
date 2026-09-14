TEMPLATE = app
CONFIG += console
TARGET   = fso_test

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
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}
INCLUDEPATH += \
	$${PWD}/

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

SOURCES += \
	../../src/filesystemhelperfunctions.cpp \
	fso_test.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp \
	qfileinfo_test.cpp \
	qdir_test.cpp

HEADERS += \
	../../src/cfilesystemobject.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h \
	QFileInfo_Test \
	QDir_Test \
	qdir_test.h \
	qfileinfo_test.h

DEFINES += CFILESYSTEMOBJECT_TEST
