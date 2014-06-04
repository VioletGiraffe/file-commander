#include "cimageviewerwidget.h"
#include "../../qtutils/imageprocessing/resize/cimageresizer.h"
#include <QFileInfo>

CImageViewerWidget::CImageViewerWidget(QWidget *parent) :
	QWidget(parent),
	_imageFileSize(0)
{
}

void CImageViewerWidget::displayImage(const QString& imagePath)
{
	_imageFileSize = 0;
	_image = QImageReader(imagePath).read();
	if (!_image.isNull())
	{
		_imageFileSize = QFileInfo(imagePath).size();
		QSize screenSize = QApplication::desktop()->availableGeometry().size() - QSize(50, 50);
		QSize windowSize = _image.size();
		if (windowSize.height() > screenSize.height() || windowSize.width() > screenSize.width())
			windowSize.scale(screenSize, Qt::KeepAspectRatio);
		resize(windowSize);
		update();
	}
}

QString CImageViewerWidget::imageInfoString() const
{
	if (_image.isNull())
		return QString();

	const int numChannels = _image.isGrayscale() ? 1 : (3 + (_image.hasAlphaChannel() ? 1 : 0));
	return QString("%1x%2, %3 channels, %4 bits per pixel, compressed to %5 bits per pixel").
			arg(_image.width()).
			arg(_image.height()).
			arg(numChannels).
			arg(_image.bitPlaneCount()).
			arg(8 * _imageFileSize / (double(_image.width()) * _image.height()));
}

QSize CImageViewerWidget::sizeHint() const
{
	return _image.size();
}

QIcon CImageViewerWidget::imageIcon(const QSize & size) const
{
	if (_image.isNull())
		return QIcon();
	return QIcon(QPixmap::fromImage(CImageResizer::resize(_image, size, CImageResizer::Bicubic)));
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	if (!_image.isNull())
		QPainter(this).drawImage(0, 0, CImageResizer::resize(_image, size(), CImageResizer::Bicubic));
}
