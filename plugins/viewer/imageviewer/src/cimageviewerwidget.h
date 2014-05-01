#ifndef CIMAGEVIEWERWIDGET_H
#define CIMAGEVIEWERWIDGET_H

#include "QtIncludes.h"

class CImageViewerWidget : public QWidget
{
	Q_OBJECT
public:
	explicit CImageViewerWidget(QWidget *parent = 0);

public:
	void displayImage(const QString& imagePath);

protected:
	void paintEvent(QPaintEvent* e);

private:
	QImage _image;
};

#endif // CIMAGEVIEWERWIDGET_H
