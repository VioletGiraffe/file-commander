TEMPLATE = lib
TARGET   = plugin_textviewer

QT = core gui
CONFIG += c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

win*{
	QT += winextras
}

mac* | linux*{
	CONFIG(release, debug|release):CONFIG += Release
	CONFIG(debug, debug|release):CONFIG += Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/textviewer
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/textviewer
UI_DIR      = ../../../build/$${OUTPUT_DIR}/textviewer
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/textviewer

INCLUDEPATH += \
	../../../file-commander-core/src \
	../../../file-commander-core/include \
	../../../qtutils \
	../../../cpputils \
	../../../text-encoding-detector/text-encoding-detector/src \
	$$PWD/src/

DEFINES += PLUGIN_MODULE

LIBS += -L../../../bin/$${OUTPUT_DIR} -lcore -lqtutils -ltext_encoding_detector -lcpputils

win*{
	QMAKE_CXXFLAGS += /MP /wd4251
	QMAKE_CXXFLAGS_WARN_ON = -W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

HEADERS += \
	src/ctextviewerplugin.h \
	src/ctextviewerwindow.h \
	src/cfinddialog.h

SOURCES += \
	src/ctextviewerplugin.cpp \
	src/ctextviewerwindow.cpp \
	src/cfinddialog.cpp


win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

FORMS += \
	src/ctextviewerwindow.ui \
	src/cfinddialog.ui

RESOURCES += \
	src/icons.qrc

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a $${DESTDIR}/libtext_encoding_detector.a
}
