#pragma once

#include "plugininterface/cpluginwindow.h"

class QLabel;

namespace Ui {
class CImageViewerWindow;
}

class CImageViewerWindow final : public CPluginWindow
{
public:
	explicit CImageViewerWindow(QWidget* parent = nullptr) noexcept;
	~CImageViewerWindow() noexcept  override;

	bool displayImage(const QString& imagePath);

private:
	QString _currentImagePath;
	Ui::CImageViewerWindow *ui;
	QLabel * _imageInfoLabel;
};
