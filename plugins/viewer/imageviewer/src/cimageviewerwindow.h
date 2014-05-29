#ifndef CIMAGEVIEWERWINDOW_H
#define CIMAGEVIEWERWINDOW_H

#include "plugininterface/cpluginwindow.h"

class QLabel;

namespace Ui {
class CImageViewerWindow;
}

class CImageViewerWindow : public CPluginWindow
{
	Q_OBJECT

public:
	explicit CImageViewerWindow(QWidget *parent = 0);
	~CImageViewerWindow();

	void displayImage(const QString& imagePath);

private:
	Ui::CImageViewerWindow *ui;
	QLabel * _imageInfoLabel;
};

#endif // CIMAGEVIEWERWINDOW_H
