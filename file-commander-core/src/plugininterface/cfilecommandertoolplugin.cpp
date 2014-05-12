#include "cfilecommandertoolplugin.h"
#include "cpluginproxy.h"

#include <assert.h>

CFileCommanderToolPlugin::CFileCommanderToolPlugin() : _proxy(0)
{
}

CFileCommanderPlugin::PluginType CFileCommanderToolPlugin::type()
{
	return Tool;
}

void CFileCommanderToolPlugin::setProxy(CPluginProxy * proxy)
{
	assert(!proxy);
	_proxy = proxy;
	proxySet();
}

void CFileCommanderToolPlugin::proxySet()
{
}
