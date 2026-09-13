#pragma once

#include "plugin_export.h"


#include <cstdint>

class CFileCommanderPlugin;
class CPluginProxy;

class QString;

// Bump on every change to the interfaces in this directory: the engine skips a plugin reporting a different value.
inline constexpr uint32_t PLUGIN_INTERFACE_VERSION = 1;

// A plugin dynamic library must implement both of these:
//   pluginInterfaceVersion - return PLUGIN_INTERFACE_VERSION;
//   createPlugin - return new CFileCommanderPluginSubclass();
extern "C" {
	// Resolved and checked before createPlugin is called: a virtual call through a mismatched vtable is undefined.
	PLUGIN_EXPORT uint32_t pluginInterfaceVersion();
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}


class PLUGIN_EXPORT CFileCommanderPlugin
{
public:
	enum PluginType {Viewer, Archive, Tool};

	CFileCommanderPlugin() noexcept;
	virtual ~CFileCommanderPlugin() noexcept = default;

	[[nodiscard]] virtual PluginType type() const = 0;
	[[nodiscard]] virtual QString name() const = 0;
	// Optional, empty by default: shown after the name wherever plugins are listed.
	[[nodiscard]] virtual QString description() const;

	void setProxy(CPluginProxy * proxy);

protected:
	// Is called after proxy has been set so that the plugin may init itself or the UI
	virtual void proxySet();

protected:
	// The engine destroys the proxy before the plugin so it can retire plugin-bound work while this instance is
	// still alive. Derived destructors must not access this pointer.
	CPluginProxy * _proxy = nullptr;
};
