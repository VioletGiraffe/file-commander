#pragma once

#include "plugininterface/cpluginwindow.h"

class QLabel;
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

private:
	void saveImageAs();

private:
	QString _currentImagePath;
	Ui::CImageViewerWindow *ui;
	QLabel * _imageInfoLabel;
	QLabel * _viewingAtLabel;
};
