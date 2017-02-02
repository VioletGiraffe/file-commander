#pragma once

#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

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
		inline MenuTree(const QString& name_, std::function<void()>&& handler_, const QIcon& icon_ = QIcon()): name(name_), icon(icon_), handler(handler_) {}

		const QString name;
		const QIcon icon;
		const std::function<void()> handler;
		std::vector<MenuTree> children;
	};

	using CreateToolMenuEntryImplementationType = std::function<void(const std::vector<CPluginProxy::MenuTree>&)>;

	CPluginProxy() = default;

// Proxy initialization (by core / UI)
	void setToolMenuEntryCreatorImplementation(const CreateToolMenuEntryImplementationType& implementation);

// UI access for plugins; every plugin is only supposed to call this method once
	void createToolMenuEntries(const std::vector<MenuTree>& menuTrees);
	void createToolMenuEntries(const MenuTree& menuTree);

// Events and data updates from the core
	void panelContentsChanged(PanelPosition panel, const QString& folder, const std::map<qulonglong /*hash*/, CFileSystemObject>& contents);

// Events and data updates from UI
	void selectionChanged(PanelPosition panel, const std::vector<qulonglong/*hash*/>& selectedItemsHashes);
	void currentItemChanged(PanelPosition panel, qulonglong currentItemHash);
	void currentPanelChanged(PanelPosition panel);

	PanelPosition currentPanel() const;
	PanelPosition otherPanel() const;

	PanelState& panelState(const PanelPosition panel);
	const PanelState& panelState(const PanelPosition panel) const;
	QString currentFolderPathForPanel(const PanelPosition panel) const;
	QString currentItemPathForPanel(const PanelPosition panel) const;
	const CFileSystemObject& currentItemForPanel(const PanelPosition panel) const;

	const CFileSystemObject& currentItem() const;
	QString currentItemPath() const;

private:
	CreateToolMenuEntryImplementationType _createToolMenuEntryImplementation;
	std::map<PanelPosition, PanelState> _panelState;
	PanelPosition                       _currentPanel = PluginUnknownPanel;
};
