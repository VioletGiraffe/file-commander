#include "cfilecommanderviewerplugin.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

CFileCommanderViewerPlugin::CFileCommanderViewerPlugin()
{
	AdvancedAssert::setLoggingFunc([](const char* message){
		qDebug() << message;
	});
}

CFileCommanderViewerPlugin::~CFileCommanderViewerPlugin()
{
}

CFileCommanderPlugin::PluginType CFileCommanderViewerPlugin::type()
{
	return Viewer;
}
