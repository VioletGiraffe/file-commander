#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <vector>

class QLibrary;
class CController;
class CPluginProxy;

class CPluginEngine final
{
public:
	explicit CPluginEngine(CController& controller) noexcept;
	~CPluginEngine();

	CPluginEngine& operator=(const CPluginEngine& other) = delete;
	CPluginEngine(const CPluginEngine& other) = delete;

	struct PluginInfo
	{
		QString name;
		QString description; // Empty unless the plugin overrides CFileCommanderPlugin::description()
		CFileCommanderPlugin::PluginType type;
	};

	void loadPlugins();
	[[nodiscard]] std::vector<PluginInfo> activePluginInfo() const;

// Operations
	void viewCurrentFile();
	void viewCurrentFileInTextViewer();
	CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> createViewerWindowForCurrentFile();

private:
	[[nodiscard]] QString currentItemPath() const;

	// An empty requiredCategory matches any viewer (auto-detect by file type, with the text viewer as fallback); a non-empty one restricts to viewers of that category().
	CFileCommanderViewerPlugin* viewerForCurrentFile(const QString& requiredCategory);

	static void showViewerWindow(CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> window);

private:
	struct LoadedPlugin
	{
		LoadedPlugin(std::unique_ptr<QLibrary> module, std::unique_ptr<CPluginProxy> proxy,
			std::unique_ptr<CFileCommanderPlugin> instance) noexcept;
		LoadedPlugin(LoadedPlugin&&) noexcept = default;
		~LoadedPlugin();

		// ~LoadedPlugin destroys the proxy first to retire its tasks while the instance and module are still alive.
		std::unique_ptr<QLibrary> module;
		std::unique_ptr<CPluginProxy> proxy;
		std::unique_ptr<CFileCommanderPlugin> instance;
	};

	CController& _controller; // Outlives the engine: main declares it first
	std::vector<LoadedPlugin> _plugins;
};
