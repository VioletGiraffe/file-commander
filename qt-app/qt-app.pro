TEMPLATE = app
TARGET   = FileCommander
DESTDIR  = ../bin

QT = core gui
CONFIG += c++11

#check Qt version
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

OBJECTS_DIR = ../build/app
MOC_DIR     = ../build/app
UI_DIR      = ../build/app
RCC_DIR     = ../build/app

INCLUDEPATH += \
	../file-commander-core/src \
	../file-commander-core/include \
	$$PWD/src/ \
	../qtutils

SOURCES += \
	src/main.cpp \
	src/cmainwindow.cpp \
	src/panel/cpanelwidget.cpp \
	src/progressdialogs/ccopymovedialog.cpp \
	src/progressdialogs/cpromptdialog.cpp \
	src/progressdialogs/cdeleteprogressdialog.cpp \
	src/panel/qflowlayout.cpp \
	src/panel/filelistwidget/model/cfilelistmodel.cpp \
	src/panel/filelistwidget/cfilelistview.cpp \
	src/panel/filelistwidget/model/cfilelistsortfilterproxymodel.cpp \
	src/settings/csettingspageinterface.cpp \
	src/settings/csettingspageedit.cpp \
    src/settings/csettingspageother.cpp \
    src/historycombobox/chistorycombobox.cpp

HEADERS += \
	src/QtAppIncludes \
	src/cmainwindow.h \
	src/panel/cpanelwidget.h \
	src/progressdialogs/ccopymovedialog.h \
	src/progressdialogs/cpromptdialog.h \
	src/progressdialogs/cdeleteprogressdialog.h \
	src/panel/qflowlayout.h \
	src/panel/filelistwidget/model/cfilelistmodel.h \
	src/panel/columns.h \
	src/panel/filelistwidget/cfilelistview.h \
	src/panel/filelistwidget/model/cfilelistsortfilterproxymodel.h \
	src/settings/csettingspageinterface.h \
	src/settings/csettingspageedit.h \
    src/settings/csettingspageother.h \
    src/historycombobox/chistorycombobox.h

FORMS += \
	src/cmainwindow.ui \
	src/panel/cpanelwidget.ui \
	src/progressdialogs/ccopymovedialog.ui \
	src/progressdialogs/cpromptdialog.ui \
	src/progressdialogs/cdeleteprogressdialog.ui \
	src/settings/csettingspageinterface.ui \
	src/settings/csettingspageedit.ui \
    src/settings/csettingspageother.ui

DEFINES += _SCL_SECURE_NO_WARNINGS

LIBS += -L../bin -lcore -lqtutils

win*{
	LIBS += -lole32 -lShell32 -lUser32
	QMAKE_CXXFLAGS += /MP
	QMAKE_CXXFLAGS_WARN_ON = -W3
}

mac*{

}

linux*{

}

linux*|mac*{
	HEADERS += src/panel/filelistwidget/cfocusframestyle.h
	SOURCES += src/panel/filelistwidget/cfocusframestyle.cpp

	QMAKE_CXXFLAGS += --std=c++11
}

win32*:!*msvc2012:*msvc* {
	QMAKE_CXXFLAGS += /FS
}
