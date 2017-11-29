TEMPLATE = lib
TARGET   = core

include(config.pri)

DESTDIR  = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lcpputils -lqtutils

mac*|linux*{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

HEADERS += \
	src/cfilesystemobject.h \
	src/ccontroller.h \
	src/fileoperationresultcode.h \
	src/cpanel.h \
	src/iconprovider/ciconprovider.h \
	src/fileoperations/operationcodes.h \
	src/fileoperations/coperationperformer.h \
	src/fileoperations/cfileoperation.h \
	src/shell/cshell.h \
	include/settings.h \
	src/favoritelocationslist/cfavoritelocations.h \
	src/filesystemhelperfunctions.h \
	src/iconprovider/ciconproviderimpl.h \
	src/fasthash.h \
	src/filesearchengine/cfilesearchengine.h \
	src/directoryscanner.h \
	src/diskenumerator/volumeinfo.hpp \
	src/diskenumerator/cvolumeenumerator.h \
	src/filesystemwatcher/cfilesystemwatcher.h \
    src/diskenumerator/volumeinfohelper.hpp

SOURCES += \
	src/cfilesystemobject.cpp \
	src/ccontroller.cpp \
	src/cpanel.cpp \
	src/iconprovider/ciconprovider.cpp \
	src/fileoperations/coperationperformer.cpp \
	src/shell/cshell.cpp \
	src/favoritelocationslist/cfavoritelocations.cpp \
	src/fasthash.c \
	src/filesearchengine/cfilesearchengine.cpp \
	src/directoryscanner.cpp \
	src/diskenumerator/cvolumeenumerator.cpp \
	src/filesystemwatcher/cfilesystemwatcher.cpp

include(src/pluginengine/pluginengine.pri)
include(src/plugininterface/plugininterface.pri)
