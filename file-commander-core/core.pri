# Links the core library and its dependencies, fileoperations.pri included.
# Must be included after the LIBS of anything that links against it: see file-operations/libraries/fileoperations.pri.

QT *= core gui widgets

INCLUDEPATH += \
	$$PWD/src \
	$$PWD/include \
	$$PWD/../qtutils

LIBS += -L$$PWD/../bin/$${OUTPUT_DIR} -lcore -lqtutils

win*: LIBS += -lole32 -lShell32 -lUser32 -lMpr
mac*: LIBS += -framework AppKit

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $$PWD/../bin/$${OUTPUT_DIR}/libcore.a $$PWD/../bin/$${OUTPUT_DIR}/libqtutils.a
}

include($$PWD/../file-operations/libraries/fileoperations.pri)
