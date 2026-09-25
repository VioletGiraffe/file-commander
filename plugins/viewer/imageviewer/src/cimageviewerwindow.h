#pragma once

#include "plugininterface/cpluginwindow.h"

class CImageViewerWidget;
class CPluginProxy;

class QAction;

class CImageViewerWindow final : public CPluginWindow
{
public:
	explicit CImageViewerWindow(CPluginProxy& proxy, QWidget* parent = nullptr) noexcept;

	bool displayImage(const QString& imagePath, bool resetViewParameters = true);

	void configureForQuickView() override;

private:
	// In quick view this window is never shown; only the widget knows the window that actually displays it.
	[[nodiscard]] QWidget* dialogParent() const;
	void saveImageAs();
	void showImageInfo();

private:
	QString _currentImagePath;
	bool _quickViewMode = false;

	CImageViewerWidget* _imageViewerWidget = nullptr;
	QAction* _imageInfoAction = nullptr;
	QAction* _saveAsAction = nullptr;
	QAction* _copyAction = nullptr;
	QAction* _copyAsDisplayedAction = nullptr;
	QAction* _fitToScreenAction = nullptr;
	QAction* _zoom1to1Action = nullptr;
	QAction* _showInfoStripAction = nullptr;
	QAction* _pixelPreservingUpscalingAction = nullptr;
};
