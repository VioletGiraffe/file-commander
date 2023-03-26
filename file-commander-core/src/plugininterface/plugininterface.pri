HEADERS += \
    src/plugininterface/cfilecommanderplugin.h \
    src/plugininterface/cfilecommanderviewerplugin.h \
    src/plugininterface/plugin_export.h \
    src/plugininterface/cpluginwindow.h \
    src/plugininterface/cpluginproxy.h \
    src/plugininterface/cfilecommandertoolplugin.h

SOURCES += \
    src/plugininterface/cfilecommanderplugin.cpp \
    src/plugininterface/cfilecommanderviewerplugin.cpp \
    src/plugininterface/cpluginwindow.cpp \
    src/plugininterface/cpluginproxy.cpp \
    src/plugininterface/cfilecommandertoolplugin.cpp

win*{
    HEADERS += \
        $$PWD/wcx/cwcxpluginhost.h \
        $$PWD/wcx/wcx_interface.h

    SOURCES += \
        $$PWD/wcx/cwcxpluginhost.cpp
}
