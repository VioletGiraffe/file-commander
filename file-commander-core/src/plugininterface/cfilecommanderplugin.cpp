#include "cfilecommanderplugin.h"
#include "cpluginproxy.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QString>
RESTORE_COMPILER_WARNINGS

CFileCommanderPlugin::CFileCommanderPlugin() noexcept
{
	AdvancedAssert::setLoggingFunc([](const char* message){
		qInfo() << message;
	});
}

QString CFileCommanderPlugin::description() const
{
	return {};
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
