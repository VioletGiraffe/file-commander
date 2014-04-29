#include "cpluginengine.h"
#include "../ccontroller.h"

#include <assert.h>

CPluginEngine::CPluginEngine() : _controller(CController::get())
{
}

void CPluginEngine::loadPlugins()
{
#if defined _WIN32
	static const QString pluginExtension(".dll");
#elif defined __linux__
	static const QString pluginExtension(".so");
#elif defined __APPLE__
	static const QString pluginExtension(".dylib");
#else
#error
#endif

	const auto pluginPaths(QDir::current().entryList((QStringList() << QString("plugin_*")+pluginExtension), QDir::Files | QDir::NoDotAndDotDot));
	for (auto& path: pluginPaths)
	{
		auto pluginModule = std::make_shared<QLibrary>(path);
		CreatePluginFunc createFunc = (CreatePluginFunc)pluginModule->resolve("createPlugin");
		if (createFunc)
		{
			CFileCommanderPlugin * plugin = createFunc();
			if (plugin)
				_plugins.emplace_back(std::make_pair(plugin, pluginModule));
		}
	}
}

const std::vector<std::pair<CFileCommanderPlugin*, std::shared_ptr<QLibrary> > >& CPluginEngine::plugins() const
{
	return _plugins;
}

void CPluginEngine::panelContentsChanged(Panel p)
{
	std::map<qulonglong /*hash*/, CFileSystemObject> contents;
	for(const CFileSystemObject& object: _controller.panel(p).list())
		contents[object.hash()] = object;

	for(auto& plugin: _plugins)
	{
		plugin.first->panelContentsChanged(pluginPanelEnumFromCorePanelEnum(p), _controller.panel(p).currentDirName(), contents);
	}
}

void CPluginEngine::selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes)
{
	for(auto& plugin: _plugins)
	{
		plugin.first->selectionChanged(pluginPanelEnumFromCorePanelEnum(p), selectedItemsHashes);
	}
}

void CPluginEngine::currentItemChanged(Panel p, qulonglong currentItemHash)
{
	for(auto& plugin: _plugins)
	{
		plugin.first->currentItemChanged(pluginPanelEnumFromCorePanelEnum(p), currentItemHash);
	}
}

CFileCommanderPlugin::PanelPosition CPluginEngine::pluginPanelEnumFromCorePanelEnum(Panel p)
{
	assert(p!=UnknownPanel);
	return p == LeftPanel ? CFileCommanderPlugin::PluginLeftPanel : CFileCommanderPlugin::PluginRightPanel;
}
