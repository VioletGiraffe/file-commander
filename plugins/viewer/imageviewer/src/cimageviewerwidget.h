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

	QIcon imageIcon(const QList<QSize>& sizes) const;

protected:
	void paintEvent(QPaintEvent* e) override;

private:
	QImage _sourceImage;
	QImage _scaledImage;
	qint64 _imageFileSize;
};

#endif // CIMAGEVIEWERWIDGET_H
