TEMPLATE = lib
CONFIG += staticlib
TARGET = qutepart

QT = core gui widgets

# A target of its own so that warn_off covers only the vendored sources: qmake has no per-file flags
CONFIG += warn_off
CONFIG -= flat

include(../../../../global.pri)
include(../3rdparty/diegoiast/qutepart-cpp/syntaxhighlighter.pri)

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR = ../../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../../build/$${OUTPUT_DIR}/$${TARGET}

QMAKE_RESOURCE_FLAGS += -threshold 10 -compress-algo best -compress 19
