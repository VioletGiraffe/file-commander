TEMPLATE = app
TARGET   = fileoperations_test
CONFIG += console

include(../../config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lqtutils -ltest_utils -lcpputils

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../test-utils/src/

for (included_item, INCLUDEPATH): INCLUDEPATH += ../../$${included_item}

DEFINES += FILE_OPERATIONS_TEST_HOOKS

# The replacement module under test, compiled directly from source.
include(../../src/fileoperations/fileoperations.pri)

SOURCES += \
	main.cpp \
	testhooktests.cpp \
	entrypathtests.cpp \
	newnamechecktests.cpp \
	filesystemmutatortests.cpp \
	stagedfilecopytests.cpp \
	destinationresolvertests.cpp \
	sourcetreebuildertests.cpp \
	transferexecutortests.cpp \
	deleteexecutortests.cpp \
	moveexecutortests.cpp \
	crossvolumetests.cpp \
	hostilenametests.cpp \
	fileoperationjobtests.cpp \
	inlinerenametests.cpp \
	../../src/filesystemhelperfunctions.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp \
	../../src/directoryscanner.cpp \
	../../src/filecomparator/filecontentcomparison.cpp \
	../../src/filecomparator/foldercomparison.cpp

HEADERS += \
	fileoperationtesthelpers.h \
	../../src/cfilesystemobject.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h \
	../../src/directoryscanner.h \
	../../src/filecomparator/filecontentcomparison.h \
	../../src/filecomparator/foldercomparison.h
