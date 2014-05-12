#ifndef CFILECOMMANDERTOOLPLUGIN_H
#define CFILECOMMANDERTOOLPLUGIN_H

#include "cfilecommanderplugin.h"

class CPluginProxy;
class CFileCommanderToolPlugin : public CFileCommanderPlugin
{
public:
	CFileCommanderToolPlugin();

	virtual PluginType type();

	void setProxy(CPluginProxy * proxy);

protected:
	virtual void proxySet();

private:
	CPluginProxy * _proxy;
};

#endif // CFILECOMMANDERTOOLPLUGIN_H
