#include "cfilecommanderplugin.h"
#include "cpluginproxy.h"

#include <assert.h>

CFileCommanderPlugin::CFileCommanderPlugin() :
	_proxy(nullptr)
{
}

CFileCommanderPlugin::~CFileCommanderPlugin()
{
}

void CFileCommanderPlugin::setProxy(CPluginProxy *proxy)
{
	assert(proxy);
	_proxy = proxy;
	proxySet();
}

void CFileCommanderPlugin::proxySet()
{
}
