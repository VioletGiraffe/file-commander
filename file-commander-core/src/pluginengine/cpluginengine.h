#ifndef CPLUGINENGINE_H
#define CPLUGINENGINE_H

#include "../cpanel.h"
#include "../plugininterface/cfilecommanderplugin.h"

#include "QtCoreIncludes"
#include <vector>
#include <memory>

class CPluginEngine : public PanelContentsChangedListener
{
public:
	static CPluginEngine& get();

	void loadPlugins();
	const std::vector<std::pair<std::shared_ptr<CFileCommanderPlugin>, std::shared_ptr<QLibrary> > >& plugins() const;

	virtual void panelContentsChanged(Panel p) override;
	void selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes);
	void currentItemChanged(Panel p, qulonglong currentItemHash);
	void currentPanelChanged(Panel p);

// Operations
	void viewCurrentFile();

private:
	CPluginEngine();
	CPluginEngine& operator=(const CPluginEngine&) {return *this;}
	static CFileCommanderPlugin::PanelPosition pluginPanelEnumFromCorePanelEnum(Panel p);

private:
	std::vector<std::pair<std::shared_ptr<CFileCommanderPlugin>, std::shared_ptr<QLibrary>>> _plugins;
};

#endif // CPLUGINENGINE_H
