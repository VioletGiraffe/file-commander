TEMPLATE = lib
TARGET   = core
DESTDIR  = ../bin

QT = core gui #gui is required for QFileIconProvider
CONFIG += staticlib c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets


OBJECTS_DIR = ../build
MOC_DIR     = ../build
UI_DIR      = ../build
RCC_DIR     = ../build

HEADERS += \
	src/cfilesystemobject.h \
	src/ccontroller.h \
	src/fileoperationresultcode.h \
	src/cpanel.h \
	src/diskenumerator/cdiskenumerator.h \
	src/iconprovider/ciconprovider.h \
	src/fileoperations/operationcodes.h \
	src/fileoperations/coperationperformer.h \
	src/fileoperations/cfileoperation.h \
	src/utils/ctimeelapsed.h \
	src/shell/cshell.h \
	include/settings.h \
	include/QtCoreIncludes \
    src/pluginengine/plugin_export.h

SOURCES += \
	src/cfilesystemobject.cpp \
	src/ccontroller.cpp \
	src/cpanel.cpp \
	src/diskenumerator/cdiskenumerator.cpp \
	src/iconprovider/ciconprovider.cpp \
	src/fileoperations/coperationperformer.cpp \
	src/utils/ctimeelapsed.cpp \
	src/shell/cshell.cpp

DEFINES += _SCL_SECURE_NO_WARNINGS

INCLUDEPATH += \
	$$PWD/include \
	../qtutils \
	../qtsystems/src/systeminfo

include(src/pluginengine/pluginengine.pri)

win*{
	QMAKE_CXXFLAGS += /MP
	QMAKE_CXXFLAGS_WARN_ON = -W4
}
mac*{

}
linux*{

}

linux*|mac*{
	QMAKE_CXXFLAGS += --std=c++11
}

LIBS += -L../bin -lqtutils
