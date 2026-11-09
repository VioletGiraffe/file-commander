TEMPLATE = lib
TARGET   = pluginapi

include(../config.pri)

# The plugin interfaces and the engine that loads plugins: the only part of the core built with widgets
QT *= gui widgets

DESTDIR = ../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../build/$${OUTPUT_DIR}/$${TARGET}

include(../src/plugininterface/plugininterface.pri)
include(../src/pluginengine/pluginengine.pri)
