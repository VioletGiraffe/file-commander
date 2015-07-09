#include "cpluginengine.h"
#include "ccontroller.h"
#include "plugininterface/cfilecommanderviewerplugin.h"
#include "plugininterface/cfilecommandertoolplugin.h"
#include "plugininterface/cpluginproxy.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLibrary>
RESTORE_COMPILER_WARNINGS

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
	QDir fileCommanderDir(qApp->applicationDirPath());

#ifdef __APPLE__
	fileCommanderDir.cdUp();
	fileCommanderDir.cdUp();
	fileCommanderDir.cdUp(); // from .app/Contents/MacOS to .app level
#endif // __APPLE__

	const auto pluginPaths(fileCommanderDir.entryInfoList((QStringList() << QString("*plugin_*")+pluginExtension+"*"), QDir::Files | QDir::NoDotAndDotDot));
	for (const QFileInfo& path: pluginPaths)
	{
		if (path.isSymLink())
			continue;

		auto pluginModule = std::make_shared<QLibrary>(path.absoluteFilePath());
		CreatePluginFunc createFunc = (CreatePluginFunc)pluginModule->resolve("createPlugin");
		if (createFunc)
		{
			CFileCommanderPlugin * plugin = createFunc();
			if (plugin)
			{
				plugin->setProxy(&CController::get().pluginProxy());
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

void CPluginEngine::panelContentsChanged(Panel p, FileListRefreshCause /*operation*/)
{
	CController& controller = CController::get();

	auto& proxy = CController::get().pluginProxy();
	proxy.panelContentsChanged(pluginPanelEnumFromCorePanelEnum(p), controller.panel(p).currentDirName(), controller.panel(p).list());
}

void CPluginEngine::itemDiscoveryInProgress(Panel /*p*/, qulonglong /*itemHash*/, size_t /*progress*/)
{
}

void CPluginEngine::selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes)
{
	auto& proxy = CController::get().pluginProxy();
	proxy.selectionChanged(pluginPanelEnumFromCorePanelEnum(p), selectedItemsHashes);
}

void CPluginEngine::currentItemChanged(Panel p, qulonglong currentItemHash)
{
	auto& proxy = CController::get().pluginProxy();
	proxy.currentItemChanged(pluginPanelEnumFromCorePanelEnum(p), currentItemHash);
}

void CPluginEngine::currentPanelChanged(Panel p)
{
	auto& proxy = CController::get().pluginProxy();
	proxy.currentPanelChanged(pluginPanelEnumFromCorePanelEnum(p));
}

void CPluginEngine::viewCurrentFile()
{
	CPluginWindow * viewerWindow = dynamic_cast<CPluginWindow*>(createViewerWindowForCurrentFile());
	if (viewerWindow)
	{
		viewerWindow->setAutoDeleteOnClose(true);
		viewerWindow->showNormal();
		viewerWindow->raise();
		viewerWindow->activateWindow();
	}
}

QMainWindow *CPluginEngine::createViewerWindowForCurrentFile()
{
	auto viewer = viewerForCurrentFile();
	return viewer ? viewer->viewCurrentFile() : nullptr;
}

PanelPosition CPluginEngine::pluginPanelEnumFromCorePanelEnum(Panel p)
{
	assert_r(p != UnknownPanel);
	return p == LeftPanel ? PluginLeftPanel : PluginRightPanel;
}

CFileCommanderViewerPlugin *CPluginEngine::viewerForCurrentFile()
{
	for(auto& plugin: _plugins)
	{
		if (plugin.first->type() == CFileCommanderPlugin::Viewer)
		{
			CFileCommanderViewerPlugin * viewer = dynamic_cast<CFileCommanderViewerPlugin*>(plugin.first.get());
			assert_r(viewer);
			if (viewer && viewer->canViewCurrentFile())
				return viewer;
		}
	}

	return nullptr;
}
