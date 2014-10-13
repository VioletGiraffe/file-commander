#include "cfilecommandertoolplugin.h"

#include <assert.h>

CFileCommanderToolPlugin::CFileCommanderToolPlugin()
{
}

CFileCommanderPlugin::PluginType CFileCommanderToolPlugin::type()
{
	return Tool;
}
