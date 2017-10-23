#pragma once

#include "cfilecommanderplugin.h"
#include "cpluginwindow.h"

class QMimeType;

class PLUGIN_EXPORT CFileCommanderViewerPlugin : public CFileCommanderPlugin
{
public:
	virtual bool canViewFile(const QString& fileName, const QMimeType& type) const = 0;
	virtual CPluginWindow* viewFile(const QString& fileName) = 0;

	PluginType type() override;
};
