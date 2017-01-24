#pragma once

#include "cpluginproxy.h"
#include "plugin_export.h"

class CFileCommanderPlugin;

// A plugin dynamic library must implement this function as follows:
// return new CFileCommanderPluginSubclass();
extern "C" {
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}


class PLUGIN_EXPORT CFileCommanderPlugin
{
public:
	enum PluginType {Viewer, Archive, Tool};

	CFileCommanderPlugin();
	virtual ~CFileCommanderPlugin() = default;

	virtual PluginType type() = 0;
	virtual QString name() const = 0;

	void setProxy(CPluginProxy * proxy);

protected:
	// Is called after proxy has been set so that the plugin may init itself or the UI
	virtual void proxySet();

protected:
	CPluginProxy * _proxy = nullptr;
};
