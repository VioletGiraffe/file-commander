#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

class CTextViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CTextViewerPlugin() = default;

	[[nodiscard]] bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	WindowPtr<CPluginWindow> viewFile(const QString& fileName) override;
	[[nodiscard]] QString name() const override;
};
