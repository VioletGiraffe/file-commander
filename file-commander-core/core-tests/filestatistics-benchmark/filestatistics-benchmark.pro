TEMPLATE = app
CONFIG += console
TARGET = filestatistics_benchmark

include(../../config.pri)

# Keeps global.pri's LTO: the scan is measured as the application builds it

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

LIBS += -L$${DESTDIR} -lqtutils -lcpputils

include(../../core.pri)

SOURCES += \
	main.cpp
