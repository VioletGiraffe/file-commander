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

# The dialog drives a real job to exact points, so the module is compiled from source with its fault hooks
# active - the same arrangement the core file-operation test project uses.
DEFINES += FILE_OPERATIONS_TEST_HOOKS
include(../../../file-commander-core/src/fileoperations/fileoperations.pri)

SOURCES += \
	main.cpp \
	fileoperationprompttests.cpp \
	fileoperationdialogtests.cpp \
	../../src/progressdialogs/cfileoperationprompt.cpp \
	../../src/progressdialogs/cfileoperationdialog.cpp \
	../../src/progressdialogs/progressdialoghelpers.cpp \
	../../../file-commander-core/src/filesystemhelperfunctions.cpp

HEADERS += \
	../../src/progressdialogs/cfileoperationprompt.h \
	../../src/progressdialogs/cfileoperationdialog.h \
	../../src/progressdialogs/cfileoperationdialogbase.h \
	../../src/progressdialogs/progressdialoghelpers.h

FORMS += \
	../../src/progressdialogs/cfileoperationprompt.ui \
	../../src/progressdialogs/cfileoperationdialog.ui
