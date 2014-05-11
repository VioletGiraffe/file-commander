#ifndef CIMAGEVIEWERWIDGET_H
#define CIMAGEVIEWERWIDGET_H

#include "QtIncludes.h"
#include "plugininterface/cpluginwindow.h"

class CImageViewerWidget : public CPluginWindow
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
