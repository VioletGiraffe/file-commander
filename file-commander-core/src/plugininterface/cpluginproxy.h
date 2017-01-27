#pragma once

#include "cfilesystemobject.h"

#include <functional>
#include <vector>
#include <map>


struct PanelState {
	std::map<qulonglong/*hash*/, CFileSystemObject> panelContents;
	std::vector<qulonglong/*hash*/>                 selectedItemsHashes;
	qulonglong                                      currentItemHash = 0;
	QString                                         currentFolder;
};

enum PanelPosition {PluginLeftPanel, PluginRightPanel, PluginUnknownPanel};

class CPluginProxy
{
public:
	struct MenuTree {
		MenuTree(const QString& name_, std::function<void()>&& handler_): name(name_), handler(handler_) {}

		QString name;
		std::function<void()> handler;
		std::vector<MenuTree> children;
	};

	using CreateToolMenuEntryImplementationType = std::function<void(const std::vector<CPluginProxy::MenuTree>&)>;

	CPluginProxy() = default;

// Proxy initialization (by core / UI)
	void setToolMenuEntryCreatorImplementation(const CreateToolMenuEntryImplementationType& implementation);

// UI access for plugins; every plugin is only supposed to call this method once
	void createToolMenuEntries(const std::vector<MenuTree>& menuEntries);

// Events and data updates from the core
	void panelContentsChanged(PanelPosition panel, const QString& folder, const std::map<qulonglong /*hash*/, CFileSystemObject>& contents);

// Events and data updates from UI
	void selectionChanged(PanelPosition panel, const std::vector<qulonglong/*hash*/>& selectedItemsHashes);
	void currentItemChanged(PanelPosition panel, qulonglong currentItemHash);
	void currentPanelChanged(PanelPosition panel);

	PanelState& currentPanelState();
	const PanelState& currentPanelState() const;
	QString currentFolderPath() const;
	QString currentItemPath() const;
	const CFileSystemObject& currentItem() const;
	bool currentItemIsFile() const;
	bool currentItemIsDir() const;

private:
	CreateToolMenuEntryImplementationType _createToolMenuEntryImplementation;
	std::map<PanelPosition, PanelState> _panelState;
	PanelPosition                       _currentPanel = PluginUnknownPanel;
};
