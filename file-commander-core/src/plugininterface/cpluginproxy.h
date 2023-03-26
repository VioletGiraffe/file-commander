#pragma once
#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <map>
#include <vector>


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
		inline MenuTree(QString name_, std::function<void()>&& handler_, QIcon icon_ = {}):
			name(std::move(name_)), icon(std::move(icon_)), handler(std::move(handler_))
		{}
		MenuTree& operator=(const MenuTree&) = delete;

		const QString name;
		const QIcon icon;
		const std::function<void()> handler;
		std::vector<MenuTree> children;
	};

	explicit CPluginProxy(std::function<void (std::function<void ()>)> execOnUiThreadImplementation);

	using CreateToolMenuEntryImplementationType = std::function<void(const std::vector<CPluginProxy::MenuTree>&)>;

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

	void execOnUiThread(const std::function<void()>& code);

private:
	CreateToolMenuEntryImplementationType _createToolMenuEntryImplementation;
	std::map<PanelPosition, PanelState> _panelState;
	std::function<void(std::function<void()>)> _execOnUiThreadImplementation;
	PanelPosition                       _currentPanel = PluginUnknownPanel;
};
