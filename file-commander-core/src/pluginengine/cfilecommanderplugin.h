#ifndef CFILECOMMANDERPLUGIN_H
#define CFILECOMMANDERPLUGIN_H

#include "../cfilesystemobject.h"
#include "plugin_export.h"

#include "QtIncludes"
#include <map>

class CFileCommanderPlugin;

typedef CFileCommanderPlugin* (*CreatePluginFunc)();

class PLUGIN_EXPORT_ONLY CFileCommanderPlugin
{
public:
	enum PanelPosition {LeftPanel, RightPanel, UnknownPanel};
	struct PanelState {
		std::map<qulonglong/*hash*/, CFileSystemObject> panelContents;
		std::vector<qulonglong/*hash*/>                 selectedItemsHashes;
		QString                                         currentFolder;
	};

	CFileCommanderPlugin();
	virtual ~CFileCommanderPlugin() = 0;

// Events and data updates from the core
	virtual void panelContentsChanged(PanelPosition panel, const QString& folder, std::map<qulonglong /*hash*/, CFileSystemObject>& contents);

// Events and data updates from UI
	virtual void selectionChanged(PanelPosition panel, std::vector<qulonglong/*hash*/> selectedItemsHashes);

protected:

protected:
	std::map<PanelPosition, PanelState> _panelState;

private:


};

#endif // CFILECOMMANDERPLUGIN_H
