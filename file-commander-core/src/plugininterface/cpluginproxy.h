#ifndef CPLUGINPROXY_H
#define CPLUGINPROXY_H

#include "../cfilesystemobject.h"

#include "QtCoreIncludes"

#include <functional>
#include <vector>
#include <map>


struct PanelState {
	PanelState() : currentItemHash(0) {}

	std::map<qulonglong/*hash*/, CFileSystemObject> panelContents;
	std::vector<qulonglong/*hash*/>                 selectedItemsHashes;
	qulonglong                                      currentItemHash;
	QString                                         currentFolder;
};

enum PanelPosition {PluginLeftPanel, PluginRightPanel, PluginUnknownPanel};

class CPluginProxy
{
public:
	struct MenuTree {
		MenuTree(const QString& name_, std::function<void()> handler_): name(name_), handler(handler_) {}

		QString name;
		std::function<void()> handler;
		std::vector<MenuTree> children;
	};

	typedef std::function<void (std::vector<MenuTree> menuEntries)> CreateToolMenuEntryImplementationType;

	CPluginProxy();

// Proxy initialization (by core / UI)
	void setToolMenuEntryCreatorImplementation(CreateToolMenuEntryImplementationType implementation);

// UI access for plugins; every plugin is only supposed to call this method once
	void createToolMenuEntries(std::vector<MenuTree> menuEntries);

// Events and data updates from the core
	void panelContentsChanged(PanelPosition panel, const QString& folder, const std::map<qulonglong /*hash*/, CFileSystemObject>& contents);

// Events and data updates from UI
	void selectionChanged(PanelPosition panel, std::vector<qulonglong/*hash*/> selectedItemsHashes);
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
	PanelPosition                       _currentPanel;
};

#endif // CPLUGINPROXY_H
