TEMPLATE = lib
TARGET   = core

include(config.pri)

DESTDIR = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

HEADERS += \
	src/batchrename/cbatchrenamesession.h \
	src/cfilesystemobject.h \
	src/ccontroller.h \
	src/detail/file_list_hashmap.h \
	src/fileoperationresultcode.h \
	src/cpanel.h \
	src/filesystemhelpers/filestatistics.h \
	src/filesystemwatcher/cdirectorysnapshotpoller.h \
	src/filesystemwatcher/cfilesystemwatcher.h \
	src/shell/cshell.h \
	src/shell/cshellcommand.h \
	include/settings.h \
	src/favoritelocationslist/cfavoritelocations.h \
	src/userprograms/userprograms.h \
	src/filesystemhelperfunctions.h \
	src/filesearchengine/cfilesearchengine.h \
	src/filesearchengine/cfilesearchresultsbuffer.h \
	src/filesearchengine/filesearchquery.h \
	src/panellisting.h \
	src/fileoperationlaunch.h \
	src/filelist/filelistsorter.h \
	src/filelist/filenamefilter.h \
	src/filelist/filesizetext.h \
	src/filelist/filetimetext.h \
	src/filelist/foldercontentssummary.h \
	src/diskenumerator/volumeinfo.hpp \
	src/diskenumerator/cvolumeenumerator.h \
	src/diskenumerator/volumeinfohelper.hpp \
	src/filecomparator/cfilecomparator.h \
	src/wcx/cwcxpluginhost_stub.h \
	src/filesystemhelpers/filesystemhelpers.hpp

SOURCES += \
	src/batchrename/cbatchrenamesession.cpp \
	src/cfilesystemobject.cpp \
	src/ccontroller.cpp \
	src/cpanel.cpp \
	src/filesystemhelperfunctions.cpp \
	src/filesystemhelpers/filestatistics.cpp \
	src/filesystemwatcher/cdirectorysnapshotpoller.cpp \
	src/filesystemwatcher/cfilesystemwatcher.cpp \
	src/shell/cshell.cpp \
	src/shell/cshellcommand.cpp \
	src/favoritelocationslist/cfavoritelocations.cpp \
	src/userprograms/userprograms.cpp \
	src/filesearchengine/cfilesearchengine.cpp \
	src/filesearchengine/cfilesearchresultsbuffer.cpp \
	src/filesearchengine/filesearchquery.cpp \
	src/panellisting.cpp \
	src/fileoperationlaunch.cpp \
	src/filelist/filelistsorter.cpp \
	src/filelist/filenamefilter.cpp \
	src/filelist/filesizetext.cpp \
	src/filelist/filetimetext.cpp \
	src/diskenumerator/cvolumeenumerator.cpp \
	src/filecomparator/cfilecomparator.cpp \
	src/filesystemhelpers/filesystemhelpers.cpp

win*{
	HEADERS += \
		src/wcx/cwcxpluginhost.h \
		src/wcx/wcxhead.h

	SOURCES += \
		src/diskenumerator/cvolumeenumerator_impl_win.cpp \
		src/wcx/cwcxpluginhost.cpp
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
