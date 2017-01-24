#pragma once

#include "cfilecommanderplugin.h"
#include "cpluginwindow.h"

class PLUGIN_EXPORT CFileCommanderViewerPlugin : public CFileCommanderPlugin
{
public:
	virtual bool canViewCurrentFile() const = 0;
	virtual CPluginWindow* viewCurrentFile() = 0;

	PluginType type() override;
};
