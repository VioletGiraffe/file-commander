TEMPLATE = app
TARGET   = filelist_test
CONFIG += console

include(../../../file-commander-core/config.pri)

# QAbstractItemModelTester
QT += testlib

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

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../../../file-commander-core/src/

# config.pri's INCLUDEPATH entries are relative to file-commander-core; rebase them for this project's location.
for (included_item, INCLUDEPATH): INCLUDEPATH += ../../../file-commander-core/$${included_item}

INCLUDEPATH += ../../../file-commander-core/core-tests/test-utils/src/

SOURCES += \
	main.cpp \
	filelistmodeltests.cpp \
	../../src/panel/filelistwidget/model/cfilelistmodel.cpp \
	../../../file-commander-core/core-tests/test-utils/src/qt_helpers.cpp

# The model's CIconProvider and fileSizeToString calls pull in the CFileSystemObject tree, as in the core fso tests.
SOURCES += \
	../../../file-commander-core/src/filesystemhelperfunctions.cpp \
	../../../file-commander-core/src/cfilesystemobject.cpp \
	../../../file-commander-core/src/iconprovider/ciconprovider.cpp \
	../../../file-commander-core/src/iconprovider/ciconproviderimpl.cpp

HEADERS += \
	../../src/panel/filelistwidget/model/cfilelistmodel.h \
	../../../file-commander-core/src/cfilesystemobject.h \
	../../../file-commander-core/src/iconprovider/ciconprovider.h \
	../../../file-commander-core/src/iconprovider/ciconproviderimpl.h
