#include "cpluginengine.h"
#include "../ccontroller.h"
#include "../plugininterface/cfilecommanderviewerplugin.h"

#include <assert.h>

CPluginEngine::CPluginEngine()
{
}

CPluginEngine& CPluginEngine::get()
{
	static CPluginEngine engine;
	return engine;
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

	const auto pluginPaths(QDir(qApp->applicationDirPath()).entryInfoList((QStringList() << QString("*plugin_*")+pluginExtension), QDir::Files | QDir::NoDotAndDotDot));
	for (auto& path: pluginPaths)
	{
		auto pluginModule = std::make_shared<QLibrary>(path.absoluteFilePath());
		CreatePluginFunc createFunc = (CreatePluginFunc)pluginModule->resolve("createPlugin");
		if (createFunc)
		{
			CFileCommanderPlugin * plugin = createFunc();
			if (plugin)
			{
				qDebug() << QString("Loaded plugin \"%1\" (%2)").arg(plugin->name()).arg(path.fileName());
				_plugins.emplace_back(std::make_pair(std::shared_ptr<CFileCommanderPlugin>(plugin), pluginModule));
			}
		}
	}
}

const std::vector<std::pair<std::shared_ptr<CFileCommanderPlugin>, std::shared_ptr<QLibrary> > >& CPluginEngine::plugins() const
{
	return _plugins;
}

void CPluginEngine::panelContentsChanged(Panel p)
{
	CController & controller = CController::get();
	std::map<qulonglong /*hash*/, CFileSystemObject> contents;
	for(const CFileSystemObject& object: controller.panel(p).list())
		contents[object.hash()] = object;

	for(auto& plugin: _plugins)
	{
		plugin.first->panelContentsChanged(pluginPanelEnumFromCorePanelEnum(p), controller.panel(p).currentDirName(), contents);
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

void CPluginEngine::currentPanelChanged(Panel p)
{
	for(auto& plugin: _plugins)
	{
		plugin.first->currentPanelChanged(pluginPanelEnumFromCorePanelEnum(p));
	}
}

void CPluginEngine::viewCurrentFile()
{
	for(auto& plugin: _plugins)
	{
		if (plugin.first->type() == CFileCommanderPlugin::Viewer)
		{
			CFileCommanderViewerPlugin * viewer = dynamic_cast<CFileCommanderViewerPlugin*>(plugin.first.get());
			assert(viewer);
			if (viewer && viewer->canViewCurrentFile())
			{
				QWidget * viewerWidget = viewer->viewCurrentFile();
				viewerWidget->showNormal();
				viewerWidget->raise();
				viewerWidget->activateWindow();
				return;
			}
		}
	}
}

CFileCommanderPlugin::PanelPosition CPluginEngine::pluginPanelEnumFromCorePanelEnum(Panel p)
{
	assert(p!=UnknownPanel);
	return p == LeftPanel ? CFileCommanderPlugin::PluginLeftPanel : CFileCommanderPlugin::PluginRightPanel;
}
