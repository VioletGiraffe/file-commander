#include "cpluginengine.h"
#include "plugininterface/cfilecommanderviewerplugin.h"
#include "plugininterface/cpluginproxy.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLibrary>
#include <QMimeDatabase>
RESTORE_COMPILER_WARNINGS

CPluginEngine::CPluginEngine(CPluginProxy& proxy) noexcept : _proxy(proxy) {}
CPluginEngine::~CPluginEngine() = default;

void CPluginEngine::loadPlugins()
{
#if defined _WIN32
	const QString pluginExtension = QStringLiteral(".dll");
#elif defined __linux__ || defined __FreeBSD__
	const QString pluginExtension = QStringLiteral(".so");
#elif defined __APPLE__
	const QString pluginExtension = QStringLiteral(".1.0.0.dylib");
#else
#error
#endif
	QDir fileCommanderDir(qApp->applicationDirPath());

	const auto pluginPaths(fileCommanderDir.entryInfoList(QStringList{"*plugin_*" + pluginExtension + "*"}, QDir::Files | QDir::NoDotAndDotDot));
	for (const QFileInfo& path: pluginPaths)
	{
		if (path.isSymLink())
			continue;

		const auto absolutePath = path.absoluteFilePath();
		auto pluginModule = std::make_unique<QLibrary>(absolutePath);
		auto createFunc = reinterpret_cast<decltype(createPlugin)*>(pluginModule->resolve("createPlugin"));
		if (createFunc)
		{
			CFileCommanderPlugin * plugin = createFunc();
			if (plugin)
			{
				plugin->setProxy(&_proxy);
				qInfo().noquote() << QStringLiteral("Loaded plugin \"%1\" (%2)").arg(plugin->name(), absolutePath);
				_plugins.push_back({std::move(pluginModule), std::unique_ptr<CFileCommanderPlugin>(plugin)});
			}
		}
	}
}

std::vector<QString> CPluginEngine::activePluginNames() const
{
	std::vector<QString> names;
	for (const auto& plugin: _plugins)
	{
		assert_r(plugin.instance);
		names.push_back(plugin.instance->name());
	}

	return names;
}

void CPluginEngine::viewCurrentFile()
{
	showViewerWindow(createViewerWindowForCurrentFile());
}

void CPluginEngine::viewCurrentFileInTextViewer()
{
	// Unlike F3, restrict the candidates to text viewer plugin(s) instead of auto-detecting across all viewers; canViewFile still applies (so e. g. folders are rejected).
	auto* viewer = viewerForCurrentFile(ViewerCategory::Text);
	if (!viewer)
		return;

	showViewerWindow(viewer->viewFile(_proxy.currentItemPath()));
}

void CPluginEngine::showViewerWindow(CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> window)
{
	auto* viewerWindow = window.release();
	if (!viewerWindow)
		return;

	viewerWindow->setAutoDeleteOnClose(true);
	viewerWindow->showNormal();
	viewerWindow->activateWindow();
	viewerWindow->raise();
}

CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> CPluginEngine::createViewerWindowForCurrentFile()
{
	auto* viewer = viewerForCurrentFile({}); // Empty category: accept any viewer, i. e. auto-detect by file type
	if (!viewer)
		return {};

	return viewer->viewFile(_proxy.currentItemPath());
}

CFileCommanderViewerPlugin* CPluginEngine::viewerForCurrentFile(const QString& requiredCategory)
{
	const QString currentFile = _proxy.currentItemPath();
	if (currentFile.isEmpty())
		return nullptr;

	const auto type = QMimeDatabase().mimeTypeForFile(currentFile, QMimeDatabase::MatchContent);
	qInfo() << "Selecting a viewer plugin for" << currentFile;
	qInfo() << "File type:" << type.name() << ", aliases:" << type.aliases();

	// The text viewer is omnivorous (its canViewFile accepts any file), so it must not shadow a specialized viewer: defer any text-category viewer and use it only if nothing more specific claims the file. This keeps selection independent of _plugins order.
	CFileCommanderViewerPlugin* textFallback = nullptr;
	for(auto& plugin: _plugins)
	{
		if (plugin.instance->type() != CFileCommanderPlugin::Viewer)
			continue;

		auto* viewer = static_cast<CFileCommanderViewerPlugin*>(plugin.instance.get());
		assert_r(viewer);
		if (!viewer)
			continue;

		const QString category = viewer->category();
		if (!requiredCategory.isEmpty() && category != requiredCategory)
			continue;

		if (!viewer->canViewFile(currentFile, type))
			continue;

		if (category == ViewerCategory::Text)
		{
			if (!textFallback)
				textFallback = viewer;

			continue;
		}

		qInfo() << viewer->name() << "selected";
		return viewer;
	}

	if (textFallback)
	{
		qInfo() << textFallback->name() << "selected";
		return textFallback;
	}

	return nullptr;
}
