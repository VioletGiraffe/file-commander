#pragma once

#include "plugin_export.h"

class CFileCommanderPlugin;
class CPluginProxy;

class QString;

// A plugin dynamic library must implement this function as follows:
// return new CFileCommanderPluginSubclass();
extern "C" {
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}

// Well-known values returned by CFileCommanderPlugin::id(), for code that must target a specific plugin regardless of UI locale.
namespace PluginId {
	inline constexpr auto TextViewer = "textviewer";
}

class PLUGIN_EXPORT CFileCommanderPlugin
{
public:
	enum PluginType {Viewer, Archive, Tool};

	CFileCommanderPlugin() noexcept;
	virtual ~CFileCommanderPlugin() noexcept = default;

	[[nodiscard]] virtual PluginType type() = 0;
	[[nodiscard]] virtual QString name() const = 0;
	// Stable, non-localized identifier (unlike the tr()-translated, display-facing name()); lets the app target a specific plugin. See PluginId.
	[[nodiscard]] virtual QString id() const = 0;

	void setProxy(CPluginProxy * proxy);

protected:
	// Is called after proxy has been set so that the plugin may init itself or the UI
	virtual void proxySet();

protected:
	CPluginProxy * _proxy = nullptr;
};
