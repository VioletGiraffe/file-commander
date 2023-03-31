TEMPLATE = lib
TARGET   = core

include(config.pri)

DESTDIR  = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lcpputils -lqtutils

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libqtutils.a $${DESTDIR}/libcpputils.a
}

HEADERS += \
	src/cfilesystemobject.h \
	src/ccontroller.h \
	src/fileoperationresultcode.h \
	src/cpanel.h \
	src/filesystemwatcher/cfilesystemwatchertimerbased.h \
	src/filesystemwatcher/cfilesystemwatcherwindows.h \
	src/iconprovider/ciconprovider.h \
	src/fileoperations/operationcodes.h \
	src/fileoperations/coperationperformer.h \
	src/fileoperations/cfileoperation.h \
	src/shell/cshell.h \
	include/settings.h \
	src/favoritelocationslist/cfavoritelocations.h \
	src/filesystemhelperfunctions.h \
	src/iconprovider/ciconproviderimpl.h \
	src/filesearchengine/cfilesearchengine.h \
	src/directoryscanner.h \
	src/diskenumerator/volumeinfo.hpp \
	src/diskenumerator/cvolumeenumerator.h \
	src/diskenumerator/volumeinfohelper.hpp \
	src/cfilemanipulator.h \
	src/filecomparator/cfilecomparator.h \
	src/filesystemhelpers/filesystemhelpers.hpp

SOURCES += \
	src/cfilesystemobject.cpp \
	src/ccontroller.cpp \
	src/cpanel.cpp \
	src/filesystemhelperfunctions.cpp \
	src/filesystemwatcher/cfilesystemwatchertimerbased.cpp \
	src/filesystemwatcher/cfilesystemwatcherwindows.cpp \
	src/iconprovider/ciconprovider.cpp \
	src/iconprovider/ciconproviderimpl.cpp \
	src/fileoperations/coperationperformer.cpp \
	src/shell/cshell.cpp \
	src/favoritelocationslist/cfavoritelocations.cpp \
	src/filesearchengine/cfilesearchengine.cpp \
	src/directoryscanner.cpp \
	src/diskenumerator/cvolumeenumerator.cpp \
	src/cfilemanipulator.cpp \
	src/filecomparator/cfilecomparator.cpp \
	src/filesystemhelpers/filesystemhelpers.cpp

win*{
	SOURCES += \
		src/diskenumerator/cvolumeenumerator_impl_win.cpp
}

mac*{
	SOURCES += \
		src/diskenumerator/cvolumeenumerator_impl_mac.cpp

	OBJECTIVE_SOURCES += \
		src/shell/cshell_mac.mm
}

linux*{
	SOURCES += \
		src/diskenumerator/cvolumeenumerator_impl_linux.cpp
}

freebsd{
	SOURCES += \
		src/diskenumerator/cvolumeenumerator_impl_freebsd.cpp
}

include(src/pluginengine/pluginengine.pri)
include(src/plugininterface/plugininterface.pri)
