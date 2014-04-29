#ifndef CPLUGINENGINE_H
#define CPLUGINENGINE_H

#include "QtIncludes"
#include <vector>
#include <memory>

class CFileCommanderPlugin;

class CPluginEngine
{
public:
	CPluginEngine();

	void loadPlugins();
	const std::vector<std::pair<CFileCommanderPlugin*, std::shared_ptr<QLibrary>>>& plugins() const;

private:
	std::vector<std::pair<CFileCommanderPlugin*, std::shared_ptr<QLibrary>>> _plugins;
};

#endif // CPLUGINENGINE_H
