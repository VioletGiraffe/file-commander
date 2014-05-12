#ifndef CPLUGINPROXY_H
#define CPLUGINPROXY_H

#include <functional>
#include <vector>
#include "QtCoreIncludes"

class CPluginProxy
{
public:
	struct MenuTree {
		MenuTree(const QString& name_, std::function<void()> handler_): name(name_), handler(handler_) {}

		QString name;
		std::function<void()> handler;
		std::vector<MenuTree> children;
	};

	typedef std::function<void (std::vector<MenuTree> menuEntries)> CreateToolMenuEntryImplementationType;

	CPluginProxy();

// Proxy initialization (by core / UI)
	void setToolMenuEntryCreatorImplementation(CreateToolMenuEntryImplementationType implementation);

// UI access for plugins; every plugin is only supposed to call this method once
	void createToolMenuEntries(std::vector<MenuTree> menuEntries);

private:
	CreateToolMenuEntryImplementationType _createToolMenuEntryImplementation;
};

#endif // CPLUGINPROXY_H
