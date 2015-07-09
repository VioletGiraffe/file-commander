#include "cfilecommanderplugin.h"
#include "cpluginproxy.h"
#include "assert/advanced_assert.h"

CFileCommanderPlugin::CFileCommanderPlugin() :
	_proxy(nullptr)
{
}

CFileCommanderPlugin::~CFileCommanderPlugin()
{
}

void CFileCommanderPlugin::setProxy(CPluginProxy *proxy)
{
	assert_r(proxy);
	_proxy = proxy;
	proxySet();
}

void CFileCommanderPlugin::proxySet()
{
}
