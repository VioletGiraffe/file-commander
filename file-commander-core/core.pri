# Links the core library and its dependencies, fileoperations.pri included.
# Must be included after the LIBS of anything that links against it: see file-operations/libraries/fileoperations.pri.

QT *= core gui widgets

INCLUDEPATH += \
	$$PWD/src \
	$$PWD/include \
	$$PWD/../qtutils

LIBS += -L$$PWD/../bin/$${OUTPUT_DIR} -lcore

win*: LIBS += -lole32 -lShell32 -lUser32 -lMpr
mac*: LIBS += -framework AppKit -framework CoreServices # CoreServices: FSEvents, for thin_io::directory_watcher

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $$PWD/../bin/$${OUTPUT_DIR}/libcore.a
}

include($$PWD/../file-operations/libraries/fileoperations.pri)
