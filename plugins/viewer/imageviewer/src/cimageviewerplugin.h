#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

class CImageViewerPlugin final : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin() = default;

	[[nodiscard]] bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	WindowPtr<CPluginWindow> viewFile(const QString& fileName) override;
	[[nodiscard]] QString name() const override;
};
