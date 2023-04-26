#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

class CImageViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin() = default;

	[[nodiscard]] bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	PluginWindowPointerType viewFile(const QString& fileName) override;
	[[nodiscard]] QString name() const override;
};
