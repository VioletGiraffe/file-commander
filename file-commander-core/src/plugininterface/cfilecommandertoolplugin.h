#ifndef CFILECOMMANDERTOOLPLUGIN_H
#define CFILECOMMANDERTOOLPLUGIN_H

#include "cfilecommanderplugin.h"

class CFileCommanderToolPlugin : public CFileCommanderPlugin
{
public:
	CFileCommanderToolPlugin();

	virtual PluginType type();
};

#endif // CFILECOMMANDERTOOLPLUGIN_H
