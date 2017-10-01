#ifndef CIMAGEVIEWERWINDOW_H
#define CIMAGEVIEWERWINDOW_H

#include "plugininterface/cpluginwindow.h"

class QLabel;

namespace Ui {
class CImageViewerWindow;
}

class CImageViewerWindow : public CPluginWindow
{
public:
	explicit CImageViewerWindow(QWidget* parent = nullptr);
	~CImageViewerWindow();

	bool displayImage(const QString& imagePath);

private:
	QString _currentImagePath;
	Ui::CImageViewerWindow *ui;
	QLabel * _imageInfoLabel;
};

#endif // CIMAGEVIEWERWINDOW_H
