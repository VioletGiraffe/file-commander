#include "cimageviewerwidget.h"

CImageViewerWidget::CImageViewerWidget(QWidget *parent) :
	QWidget(parent)
{
}

void CImageViewerWidget::displayImage(const QString& imagePath)
{
	_image = QImageReader(imagePath).read();
	if (!_image.isNull())
		update();
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	if (!_image.isNull())
		QPainter(this).drawImage(0, 0, _image.scaled(width(), height(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
