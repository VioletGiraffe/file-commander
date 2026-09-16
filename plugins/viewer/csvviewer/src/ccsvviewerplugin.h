#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

class CCsvViewerPlugin final : public CFileCommanderViewerPlugin
{
public:
	CCsvViewerPlugin() = default;

	[[nodiscard]] bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	WindowPtr<CPluginWindow> viewFile(const QString& fileName) override;
	[[nodiscard]] QString name() const override;
	[[nodiscard]] QString category() const override;
};
