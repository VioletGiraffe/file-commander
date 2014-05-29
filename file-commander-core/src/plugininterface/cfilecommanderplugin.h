#ifndef CFILECOMMANDERPLUGIN_H
#define CFILECOMMANDERPLUGIN_H

#include "../../file-commander-core/src/cfilesystemobject.h"
#include "plugin_export.h"

#include "QtCoreIncludes"
#include <map>

class CFileCommanderPlugin;

typedef CFileCommanderPlugin* (*CreatePluginFunc)();

class PLUGIN_EXPORT CFileCommanderPlugin
{
public:

	enum PanelPosition {PluginLeftPanel, PluginRightPanel, PluginUnknownPanel};

	enum PluginType {Viewer, Archive, Tool};

	struct PanelState {
		PanelState() : currentItemHash(0) {}

		std::map<qulonglong/*hash*/, CFileSystemObject> panelContents;
		std::vector<qulonglong/*hash*/>                 selectedItemsHashes;
		qulonglong                                      currentItemHash;
		QString                                         currentFolder;
	};

	CFileCommanderPlugin();
	virtual ~CFileCommanderPlugin() = 0;

	virtual PluginType type() = 0;
	virtual QString name();

// Events and data updates from the core
	virtual void panelContentsChanged(PanelPosition panel, const QString& folder, std::map<qulonglong /*hash*/, CFileSystemObject>& contents);

// Events and data updates from UI
	virtual void selectionChanged(PanelPosition panel, std::vector<qulonglong/*hash*/> selectedItemsHashes);
	virtual void currentItemChanged(PanelPosition panel, qulonglong currentItemHash);
	virtual void currentPanelChanged(PanelPosition panel);

protected:
	PanelState& currentPanelState();
	const PanelState& currentPanelState() const;
	QString currentFolderPath() const;
	QString currentItemPath() const;
	const CFileSystemObject& currentItem() const;
	bool currentItemIsFile() const;
	bool currentItemIsDir() const;

protected:
	std::map<PanelPosition, PanelState> _panelState;
	PanelPosition                       _currentPanel;
};

#endif // CFILECOMMANDERPLUGIN_H
