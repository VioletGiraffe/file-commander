#pragma once

#include "cfilecommanderplugin.h"
#include "cpluginwindow.h"

#include <memory>

class QMimeType;

class PLUGIN_EXPORT CFileCommanderViewerPlugin : public CFileCommanderPlugin
{
public:
	using PluginWindowPointerType = std::unique_ptr<CPluginWindow, std::function<void(CPluginWindow*)>>;

	[[nodiscard]] virtual bool canViewFile(const QString& fileName, const QMimeType& type) const = 0;
	virtual PluginWindowPointerType viewFile(const QString& fileName) = 0;

	PluginType type() override;
};
