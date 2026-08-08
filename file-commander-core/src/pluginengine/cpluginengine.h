#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

#include <memory>
#include <vector>

class QLibrary;
class CPluginProxy;

class CPluginEngine final
{
public:
	explicit CPluginEngine(CPluginProxy& proxy) noexcept;
	~CPluginEngine();

	CPluginEngine& operator=(const CPluginEngine& other) = delete;
	CPluginEngine(const CPluginEngine& other) = delete;

	void loadPlugins();
	std::vector<QString> activePluginNames() const;

// Operations
	void viewCurrentFile();
	void viewCurrentFileInTextViewer();
	CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> createViewerWindowForCurrentFile();

private:
	// An empty requiredCategory matches any viewer (auto-detect by file type, with the text viewer as fallback); a non-empty one restricts to viewers of that category().
	CFileCommanderViewerPlugin* viewerForCurrentFile(const QString& requiredCategory);

	static void showViewerWindow(CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> window);

private:
	struct LoadedPlugin
	{
		std::unique_ptr<QLibrary> module; // declared before instance so that they are destroyed in the correct (reverse) order
		std::unique_ptr<CFileCommanderPlugin> instance;
	};

	CPluginProxy& _proxy; // Controller-owned; main's declaration order keeps it alive through plugin destruction
	std::vector<LoadedPlugin> _plugins;
};
