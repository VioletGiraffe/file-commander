#include "cpluginengine.h"
#include "ccontroller.h"
#include "plugininterface/cfilecommanderviewerplugin.h"
#include "plugininterface/cfilecommandertoolplugin.h"
#include "plugininterface/cpluginproxy.h"
#include "container/algorithms.hpp"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLibrary>
#include <QMimeDatabase>
RESTORE_COMPILER_WARNINGS

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
		auto createFunc = (decltype(createPlugin)*)(pluginModule->resolve("createPlugin"));
		if (createFunc)
		{
			CFileCommanderPlugin * plugin = createFunc();
			if (plugin)
			{
				plugin->setProxy(&CController::get().pluginProxy());
				qInfo() << QString("Loaded plugin \"%1\" (%2)").arg(plugin->name()).arg(path.fileName());
				_plugins.emplace_back(std::make_pair(std::shared_ptr<CFileCommanderPlugin>(plugin), pluginModule));
			}
		}
	}
}

std::vector<QString> CPluginEngine::activePluginNames()
{
	std::vector<QString> names;
	for (const auto& plugin: _plugins)
	{
		assert_r(plugin.first.get());
		names.push_back(plugin.first->name());
	}

	return names;
}

void CPluginEngine::panelContentsChanged(Panel p, FileListRefreshCause /*operation*/)
{
	CController& controller = CController::get();

	auto& proxy = CController::get().pluginProxy();
	proxy.panelContentsChanged(pluginPanelEnumFromCorePanelEnum(p), controller.panel(p).currentDirPathPosix(), controller.panel(p).list());
}

void CPluginEngine::itemDiscoveryInProgress(Panel /*p*/, qulonglong /*itemHash*/, size_t /*progress*/, const QString& /*currentDir*/)
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
	CPluginWindow * viewerWindow = createViewerWindowForCurrentFile();
	if (viewerWindow)
	{
		viewerWindow->setAutoDeleteOnClose(true);
		viewerWindow->showNormal();
		viewerWindow->activateWindow();
		viewerWindow->raise();
	}
}

CPluginWindow *CPluginEngine::createViewerWindowForCurrentFile()
{
	auto viewer = viewerForCurrentFile();
	if (!viewer)
		return nullptr;

	CPluginWindow * window = viewer->viewFile(CController::get().pluginProxy().currentItemPath());
	if (!window)
		return nullptr;

	_activeWindows.push_back(window);
	window->connect(window, &QObject::destroyed, [this](QObject* object) {
		ContainerAlgorithms::erase_all_occurrences(_activeWindows, object);
	});
	return window;
}

PanelPosition CPluginEngine::pluginPanelEnumFromCorePanelEnum(Panel p)
{
	assert_r(p != UnknownPanel);
	return p == LeftPanel ? PluginLeftPanel : PluginRightPanel;
}

CFileCommanderViewerPlugin *CPluginEngine::viewerForCurrentFile()
{
	const QString currentFile = CController::get().pluginProxy().currentItemPath();
	const auto type = QMimeDatabase().mimeTypeForFile(currentFile, QMimeDatabase::MatchContent);
	qInfo() << "Selecting a viewer plugin for" << currentFile;
	qInfo() << "File type:" << type.name() << ", aliases:" << type.aliases();

	for(auto& plugin: _plugins)
	{
		if (plugin.first->type() == CFileCommanderPlugin::Viewer)
		{
			CFileCommanderViewerPlugin * viewer = static_cast<CFileCommanderViewerPlugin*>(plugin.first.get());
			assert_r(viewer);
			if (viewer && viewer->canViewFile(currentFile, type))
			{
				qInfo() << viewer->name() << "selected";
				return viewer;
			}
		}
	}

	return nullptr;
}

void CPluginEngine::destroyAllPluginWindows()
{
	const auto tmpWindowsList = _activeWindows;
	for (QWidget* window: tmpWindowsList)
		delete window;
}
