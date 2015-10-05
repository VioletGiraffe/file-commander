TEMPLATE = app
TARGET   = FileCommander

QT = core gui widgets

win*{
    QT += winextras
}

CONFIG += c++11

mac* | linux*{
    CONFIG(release, debug|release):CONFIG += Release
    CONFIG(debug, debug|release):CONFIG += Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../build/$${OUTPUT_DIR}/app
MOC_DIR     = ../build/$${OUTPUT_DIR}/app
UI_DIR      = ../build/$${OUTPUT_DIR}/app
RCC_DIR     = ../build/$${OUTPUT_DIR}/app

INCLUDEPATH += \
    $$PWD/src/ \
    ../file-commander-core/src \
    ../file-commander-core/include \
    ../qtutils \
    ../cpputils

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
    src/progressdialogs/cdeleteprogressdialog.cpp

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
    src/progressdialogs/cdeleteprogressdialog.h

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
    src/progressdialogs/cdeleteprogressdialog.ui


DEFINES += _SCL_SECURE_NO_WARNINGS

LIBS += -L../bin/$${OUTPUT_DIR} -lcore -lqtutils -lcpputils

win*{
    LIBS += -lole32 -lShell32 -lUser32
    QMAKE_CXXFLAGS += /MP /wd4251
    QMAKE_CXXFLAGS_WARN_ON = /W4
    DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

    RC_FILE = resources/file_commander.rc
}

mac*{
    ICON = resources/file_commander.icns
}

linux*{

}

linux*|mac*{
    HEADERS += src/panel/filelistwidget/cfocusframestyle.h
    SOURCES += src/panel/filelistwidget/cfocusframestyle.cpp

    QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

    CONFIG(release, debug|release):CONFIG += Release
    CONFIG(debug, debug|release):CONFIG += Debug

    Release:DEFINES += NDEBUG=1
    Debug:DEFINES += _DEBUG
}

win32*:!*msvc2012:*msvc* {
    QMAKE_CXXFLAGS += /FS
}


mac*|linux*{
    PRE_TARGETDEPS += $${DESTDIR}/libcore.a
}
