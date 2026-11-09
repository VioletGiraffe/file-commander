# Links the plugin API library (the plugin interfaces and the plugin engine) and the core it builds on, core.pri included.
# Must be included after the LIBS of anything that links against it: see file-operations/libraries/fileoperations.pri.

QT *= core gui widgets

LIBS += -L$$PWD/../bin/$${OUTPUT_DIR} -lpluginapi

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $$PWD/../bin/$${OUTPUT_DIR}/libpluginapi.a
}

include($$PWD/core.pri)
