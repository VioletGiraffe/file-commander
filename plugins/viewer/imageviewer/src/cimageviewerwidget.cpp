#include "cimageviewerwidget.h"

CImageViewerWidget::CImageViewerWidget(QWidget *parent) :
	QWidget(parent)
{
	new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
}

void CImageViewerWidget::displayImage(const QString& imagePath)
{
	_image = QImageReader(imagePath).read();
	if (!_image.isNull())
	{
		QSize screenSize = QApplication::desktop()->availableGeometry().size() - QSize(50, 50);
		QSize windowSize = _image.size();
		if (windowSize.height() > screenSize.height() || windowSize.width() > screenSize.width())
			windowSize.scale(screenSize, Qt::KeepAspectRatio);
		resize(windowSize);
		update();
	}
}

void CImageViewerWidget::closeEvent(QCloseEvent* event)
{
	if (event)
		emit closed();
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	if (!_image.isNull())
		QPainter(this).drawImage(0, 0, _image.scaled(width(), height(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
