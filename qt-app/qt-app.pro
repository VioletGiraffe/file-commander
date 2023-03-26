TEMPLATE = app
TARGET   = FileCommander

QT = core gui widgets network
lessThan(QT_MAJOR_VERSION, 6) {
	win*:QT += winextras
}

CONFIG += strict_c++ c++2a

mac* | linux* | freebsd{
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

contains(QT_ARCH, x86_64) {
	ARCHITECTURE = x64
} else {
	ARCHITECTURE = x86
}

android {
	Release:OUTPUT_DIR=android/release
	Debug:OUTPUT_DIR=android/debug

} else:ios {
	Release:OUTPUT_DIR=ios/release
	Debug:OUTPUT_DIR=ios/debug

} else {
	Release:OUTPUT_DIR=release/$${ARCHITECTURE}
	Debug:OUTPUT_DIR=debug/$${ARCHITECTURE}
}

DESTDIR  = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../build/$${OUTPUT_DIR}/$${TARGET}

INCLUDEPATH += \
	$$PWD/src/ \
	../file-commander-core/src \
	../file-commander-core/include \
	../qtutils \
	../cpputils \
	../cpp-template-utils \
	../github-releases-autoupdater/src

SOURCES += \
	src/main.cpp \
	src/cmainwindow.cpp \
	src/panel/cpanelwidget.cpp \
	src/progressdialogs/ccopymovedialog.cpp \
	src/progressdialogs/cpromptdialog.cpp \
	src/panel/qflowlayout.cpp \
	src/panel/filelistwidget/model/cfilelistmodel.cpp \
	src/panel/filelistwidget/cfilelistview.cpp \
	src/panel/filelistwidget/model/cfilelistsortfilterproxymodel.cpp \
	src/settings/csettingspageinterface.cpp \
	src/settings/csettingspageedit.cpp \
	src/settings/csettingspageother.cpp \
	src/panel/filelistwidget/delegate/cfilelistitemdelegate.cpp \
	src/progressdialogs/cfileoperationconfirmationprompt.cpp \
	src/settings/csettingspageoperations.cpp \
	src/favoritelocationseditor/cfavoritelocationseditor.cpp \
	src/favoritelocationseditor/cnewfavoritelocationdialog.cpp \
	src/panel/filelistwidget/cfilelistfilterdialog.cpp \
	src/filessearchdialog/cfilessearchwindow.cpp \
	src/progressdialogs/cdeleteprogressdialog.cpp \
	src/aboutdialog/caboutdialog.cpp \
	src/progressdialogs/progressdialoghelpers.cpp \
	src/panel/cpaneldisplaycontroller.cpp

HEADERS += \
	src/cmainwindow.h \
	src/panel/cpanelwidget.h \
	src/progressdialogs/ccopymovedialog.h \
	src/progressdialogs/cpromptdialog.h \
	src/panel/qflowlayout.h \
	src/panel/filelistwidget/model/cfilelistmodel.h \
	src/panel/columns.h \
	src/panel/filelistwidget/cfilelistview.h \
	src/panel/filelistwidget/model/cfilelistsortfilterproxymodel.h \
	src/settings/csettingspageinterface.h \
	src/settings/csettingspageedit.h \
	src/settings/csettingspageother.h \
	src/panel/filelistwidget/delegate/cfilelistitemdelegate.h \
	src/progressdialogs/cfileoperationconfirmationprompt.h \
	src/settings/csettingspageoperations.h \
	src/favoritelocationseditor/cfavoritelocationseditor.h \
	src/favoritelocationseditor/cnewfavoritelocationdialog.h \
	src/panel/filelistwidget/cfilelistfilterdialog.h \
	src/filessearchdialog/cfilessearchwindow.h \
	src/progressdialogs/cdeleteprogressdialog.h \
	src/version.h \
	src/aboutdialog/caboutdialog.h \
	src/progressdialogs/progressdialoghelpers.h \
	src/panel/cpaneldisplaycontroller.h

FORMS += \
	src/cmainwindow.ui \
	src/panel/cpanelwidget.ui \
	src/progressdialogs/ccopymovedialog.ui \
	src/progressdialogs/cpromptdialog.ui \
	src/settings/csettingspageinterface.ui \
	src/settings/csettingspageedit.ui \
	src/settings/csettingspageother.ui \
	src/progressdialogs/cfileoperationconfirmationprompt.ui \
	src/settings/csettingspageoperations.ui \
	src/favoritelocationseditor/cfavoritelocationseditor.ui \
	src/favoritelocationseditor/cnewfavoritelocationdialog.ui \
	src/panel/filelistwidget/cfilelistfilterdialog.ui \
	src/filessearchdialog/cfilessearchwindow.ui \
	src/progressdialogs/cdeleteprogressdialog.ui \
	src/aboutdialog/caboutdialog.ui


DEFINES += _SCL_SECURE_NO_WARNINGS

LIBS += -L../bin/$${OUTPUT_DIR} -lautoupdater -lcore -lqtutils -lcpputils

win*{
	LIBS += -lole32 -lShell32 -lUser32
	QMAKE_CXXFLAGS += /MP /Zi /wd4251 /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = /W4
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	!*msvc2013*:QMAKE_LFLAGS += /DEBUG:FASTLINK

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF

	RC_FILE = resources/file_commander.rc
}

mac*{
	ICON = resources/file_commander.icns

	LIBS += -framework AppKit

		QMAKE_POST_LINK = cp -f -p $$OUT_PWD/$$DESTDIR/*.dylib $$OUT_PWD/$$DESTDIR/$${TARGET}.app/Contents/MacOS/
}

linux*|mac*|freebsd{
	HEADERS += src/panel/filelistwidget/cfocusframestyle.h
	SOURCES += src/panel/filelistwidget/cfocusframestyle.cpp

	QMAKE_CXXFLAGS_WARN_ON = -Wall

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}


mac*|linux*|freebsd{
	PRE_TARGETDEPS += $${DESTDIR}/libcore.a
}

RESOURCES += \
	resources/app-resources.qrc

linux*|freebsd{
	#Installation

	isEmpty(PREFIX) {
		PREFIX = $${DESTDIR}/installation
	}
	target.path = $${PREFIX}/bin

	desktop.path = $${PREFIX}/share/applications/
	desktop.files += file_commander.desktop
	icon256.path = $${PREFIX}/share/icons/hicolor/256x256/apps
	icon256.files += resources\icon.png

	INSTALLS += icon256
	INSTALLS += desktop
	INSTALLS += target
}
