#ifndef CPLUGINENGINE_H
#define CPLUGINENGINE_H

#include "../cpanel.h"
#include "cfilecommanderplugin.h"

#include "QtIncludes"
#include <vector>
#include <memory>

class CController;

class CPluginEngine : public PanelContentsChangedListener
{
public:
	CPluginEngine();

	void loadPlugins();
	const std::vector<std::pair<CFileCommanderPlugin*, std::shared_ptr<QLibrary>>>& plugins() const;

	virtual void panelContentsChanged(Panel p);
	virtual void selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes);
	virtual void currentItemChanged(Panel p, qulonglong currentItemHash);

private:
	CPluginEngine& operator=(const CPluginEngine&) {}
	static CFileCommanderPlugin::PanelPosition pluginPanelEnumFromCorePanelEnum(Panel p);

private:
	std::vector<std::pair<CFileCommanderPlugin*, std::shared_ptr<QLibrary>>> _plugins;

	CController & _controller;
};

#endif // CPLUGINENGINE_H
