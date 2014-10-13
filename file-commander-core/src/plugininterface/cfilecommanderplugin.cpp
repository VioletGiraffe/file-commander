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

QString CFileCommanderPlugin::name()
{
	return QString();
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
