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
	QString imageInfoString() const;

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* e) override;

private:
	QImage  _image;
	qint64  _imageFileSize;
};

#endif // CIMAGEVIEWERWIDGET_H
