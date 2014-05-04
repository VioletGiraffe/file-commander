#ifndef CIMAGEVIEWERWIDGET_H
#define CIMAGEVIEWERWIDGET_H

#include "QtIncludes.h"
#include "plugininterface/cviewerwindow.h"

class CImageViewerWidget : public CViewerWindow
{
	Q_OBJECT
public:
	explicit CImageViewerWidget(QWidget *parent = 0);

public:
	void displayImage(const QString& imagePath);

protected:
	void paintEvent(QPaintEvent* e) override;

private:
	QImage _image;
};

#endif // CIMAGEVIEWERWIDGET_H
