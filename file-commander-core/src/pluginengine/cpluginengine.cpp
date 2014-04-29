#include "cpluginengine.h"
#include "cfilecommanderplugin.h"

CPluginEngine::CPluginEngine()
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
