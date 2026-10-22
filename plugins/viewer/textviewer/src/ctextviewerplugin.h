#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

class CTextViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CTextViewerPlugin() = default;

	[[nodiscard]] bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	[[nodiscard]] std::unique_ptr<CFileViewer> createViewer() override;
	[[nodiscard]] QString name() const override;
	[[nodiscard]] QString category() const override;
};
