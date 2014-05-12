#include "cpluginproxy.h"

CPluginProxy::CPluginProxy()
{
}

void CPluginProxy::setToolMenuEntryCreatorImplementation(CPluginProxy::CreateToolMenuEntryImplementationType implementation)
{
	_createToolMenuEntryImplementation = implementation;
}

void CPluginProxy::createToolMenuEntries(std::vector<MenuTree> menuEntries)
{
	if (_createToolMenuEntryImplementation)
		_createToolMenuEntryImplementation(menuEntries);
}
