#pragma once

#include "plugininterface/cfileviewer.h"

class CImageViewerWidget;
class CPluginProxy;

class CImageViewer final : public CFileViewer
{
public:
	explicit CImageViewer(CPluginProxy& proxy) noexcept;

	void closeFile() override;
	[[nodiscard]] bool sizesToContent() const override { return true; }

protected:
	QString tryLoadFile(const QString& path) override;

private:
	void saveImageAs();
	void showImageInfo();

private:
	QString _currentImagePath;
	CImageViewerWidget* _imageViewerWidget = nullptr;
};
