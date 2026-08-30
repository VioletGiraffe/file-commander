#pragma once

#include "plugininterface/cpluginwindow.h"

class CPluginProxy;

namespace Ui {
class CImageViewerWindow;
}

class CImageViewerWindow final : public CPluginWindow
{
public:
	explicit CImageViewerWindow(CPluginProxy& proxy, QWidget* parent = nullptr) noexcept;
	~CImageViewerWindow() noexcept override;

	bool displayImage(const QString& imagePath);

	void configureForQuickView() override;

private:
	// In quick view this window is never shown; only the widget knows the window that actually displays it.
	[[nodiscard]] QWidget* dialogParent() const;
	void saveImageAs();

private:
	QString _currentImagePath;
	bool _quickViewMode = false;
	Ui::CImageViewerWindow *ui;
};
