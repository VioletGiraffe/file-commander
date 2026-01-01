#pragma once

#include "cpanel.h"
#include "plugininterface/cfilecommanderviewerplugin.h"

#include <memory>
#include <vector>

enum PanelPosition : int;
class QLibrary;

class CPluginEngine final : public PanelContentsChangedListener
{
public:
	CPluginEngine();
	~CPluginEngine() override;

	CPluginEngine& operator=(const CPluginEngine& other) = delete;
	CPluginEngine(const CPluginEngine& other) = delete;

	static CPluginEngine& get();

	void loadPlugins();
	std::vector<QString> activePluginNames();

	// CPanel observers
	void onPanelContentsChanged(Panel p, FileListRefreshCause operation) override;

	void selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes);
	void currentItemChanged(Panel p, qulonglong currentItemHash);
	void currentPanelChanged(Panel p);

// Operations
	void viewCurrentFile();
	CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> createViewerWindowForCurrentFile();

private:
	static PanelPosition pluginPanelEnumFromCorePanelEnum(Panel p);

	CFileCommanderViewerPlugin * viewerForCurrentFile();

private:
	std::vector<std::pair<std::unique_ptr<CFileCommanderPlugin>, std::unique_ptr<QLibrary>>> _plugins;
};
