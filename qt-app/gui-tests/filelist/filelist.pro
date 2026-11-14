TEMPLATE = app
TARGET   = filelist_test
CONFIG += console

include(../../../file-commander-core/config.pri)

# testlib: QAbstractItemModelTester
# svg: the file glyphs go through qtutils' CTintedSvgIconEngine
QT += widgets testlib svg

# global.pri's LTO removed: only tests with benchmarks use LTO
# MSVC keeps /GL and /LTCG: linking the /GL-built qtutils, cpputils and thin_io forces LTCG
QMAKE_CFLAGS   -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_CXXFLAGS -= -flto=auto -ffat-lto-objects -flto=thin
QMAKE_LFLAGS   -= -flto=auto -flto=thin

# C4702 in Qt's qnumeric.h comes from link-time code generation, which the warning pragmas around Qt includes don't cover
win*:QMAKE_CXXFLAGS += /wd4702

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lqtutils -lcpputils
include(../../../cpp-template-utils/3rdparty/catch2/catch2.pri)

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../../../file-commander-core/src/

INCLUDEPATH += ../../../file-operations/test-utils/src/

include(../../../file-commander-core/core.pri)

SOURCES += \
	main.cpp \
	filelistmodeltests.cpp \
	filelistupdatetests.cpp \
	../../src/iconprovider/ciconprovider.cpp \
	../../src/iconprovider/ciconproviderimpl.cpp \
	../../src/panel/filelistwidget/cfilelistview.cpp \
	../../src/panel/filelistwidget/delegate/cfilelistitemdelegate.cpp \
	../../src/panel/filelistwidget/model/cfilelistmodel.cpp \
	../../../file-operations/test-utils/src/crandomdatagenerator.cpp \
	../../../file-operations/test-utils/src/qt_helpers.cpp

HEADERS += \
	filelisttesthelpers.h \
	../../src/iconprovider/ciconprovider.h \
	../../src/iconprovider/ciconproviderimpl.h \
	../../src/iconprovider/fileglyph.h \
	../../src/panel/filelistwidget/cfilelistview.h \
	../../src/panel/filelistwidget/filelistlook.h \
	../../src/panel/filelistwidget/delegate/cfilelistitemdelegate.h \
	../../src/panel/filelistwidget/model/cfilelistmodel.h
