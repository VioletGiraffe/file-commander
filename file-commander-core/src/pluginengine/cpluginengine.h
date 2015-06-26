#ifndef CPLUGINENGINE_H
#define CPLUGINENGINE_H

#include "../cpanel.h"
#include "../plugininterface/cfilecommanderplugin.h"

#include "QtCoreIncludes"
#include <QLibrary>
#include <QMainWindow>

#include <vector>
#include <memory>

class CFileCommanderViewerPlugin;

class CPluginEngine : public PanelContentsChangedListener
{
public:
	static CPluginEngine& get();

	void loadPlugins();
	const std::vector<std::pair<std::shared_ptr<CFileCommanderPlugin>, std::shared_ptr<QLibrary> > >& plugins() const;

	// CPanel observers
	void panelContentsChanged(Panel p, FileListRefreshCause operation) override;
	void itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress) override;

	void selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes);
	void currentItemChanged(Panel p, qulonglong currentItemHash);
	void currentPanelChanged(Panel p);

// Operations
	void viewCurrentFile();
	QMainWindow * createViewerWindowForCurrentFile();

private:
	CPluginEngine();
	CPluginEngine& operator=(const CPluginEngine&) {return *this;}
	static PanelPosition pluginPanelEnumFromCorePanelEnum(Panel p);

	CFileCommanderViewerPlugin * viewerForCurrentFile();

private:
	std::vector<std::pair<std::shared_ptr<CFileCommanderPlugin>, std::shared_ptr<QLibrary>>> _plugins;
};

#endif // CPLUGINENGINE_H
