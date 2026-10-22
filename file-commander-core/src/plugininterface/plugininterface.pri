HEADERS += \
	$$PWD/wcx/cwcxpluginhost_stub.h \
	src/plugininterface/cfilecommanderplugin.h \
	src/plugininterface/cfilecommanderviewerplugin.h \
	src/plugininterface/plugin_export.h \
	src/plugininterface/cfileviewer.h \
	src/plugininterface/cpluginproxy.h \
	src/plugininterface/cfilecommandertoolplugin.h

SOURCES += \
	src/plugininterface/cfilecommanderplugin.cpp \
	src/plugininterface/cfilecommanderviewerplugin.cpp \
	src/plugininterface/cfileviewer.cpp \
	src/plugininterface/cpluginproxy.cpp \
	src/plugininterface/cfilecommandertoolplugin.cpp

win*{
	HEADERS += \
		$$PWD/wcx/cwcxpluginhost.h \
		$$PWD/wcx/wcxhead.h \

	SOURCES += \
		$$PWD/wcx/cwcxpluginhost.cpp
}
