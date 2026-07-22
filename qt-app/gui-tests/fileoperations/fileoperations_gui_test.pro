TEMPLATE = app
TARGET   = fileoperations_gui_test
CONFIG += console

include(../../../file-commander-core/config.pri)

DESTDIR  = ../../../bin/$${OUTPUT_DIR}
DESTDIR_NOARCH  = ../../../bin/$${OUTPUT_DIR_NOARCH}
OBJECTS_DIR = ../../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../../build/$${OUTPUT_DIR}/$${TARGET}

LIBS += -L$${DESTDIR} -lqtutils
LIBS += -L$${DESTDIR_NOARCH} -lcpputils

mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR_NOARCH}/libqtutils.a $${DESTDIR_NOARCH}/libcpputils.a
}

INCLUDEPATH += \
	../../src/ \
	../../../file-commander-core/src/

# config.pri's INCLUDEPATH entries are relative to file-commander-core; rebase them for this project's location.
for (included_item, INCLUDEPATH): INCLUDEPATH += ../../../file-commander-core/$${included_item}

# The tested UI sources and their minimal core dependencies, compiled directly from source.
SOURCES += \
	main.cpp \
	fileoperationprompttests.cpp \
	../../src/progressdialogs/cfileoperationprompt.cpp \
	../../../file-commander-core/src/fileoperations/fileoperationtypes.cpp \
	../../../file-commander-core/src/fileoperations/centrypath.cpp \
	../../../file-commander-core/src/filesystemhelperfunctions.cpp

HEADERS += \
	../../src/progressdialogs/cfileoperationprompt.h

FORMS += \
	../../src/progressdialogs/cfileoperationprompt.ui
