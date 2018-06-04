TEMPLATE = app
TARGET   = fso_test

include(../../config.pri)

QT = core testlib
QT += gui winextras #QIcon, iconprovider

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
	fso_test.cpp \
	../../src/cfilesystemobject.cpp \
	../../src/fasthash.c \
	../../src/iconprovider/ciconprovider.cpp \
	qfileinfo_test.cpp \
	qdir_test.cpp

HEADERS += \
	../../src/cfilesystemobject.h \
	../../src/fasthash.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h \
	QFileInfo_Test \
	QDir_Test \
    qdir_test.h \
    qfileinfo_test.h

DEFINES += CFILESYSTEMOBJECT_TEST
