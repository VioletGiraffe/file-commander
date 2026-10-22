#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

class CImageViewerPlugin final : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin() = default;

	[[nodiscard]] bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	[[nodiscard]] std::unique_ptr<CFileViewer> createViewer() override;
	[[nodiscard]] QString name() const override;
	[[nodiscard]] QString category() const override;
};
