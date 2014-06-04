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
	_sourceImage = QImageReader(imagePath).read();
	if (!_sourceImage.isNull())
	{
		_imageFileSize = QFileInfo(imagePath).size();
		QSize screenSize = QApplication::desktop()->availableGeometry().size() - QSize(50, 50);
		QSize windowSize = _sourceImage.size();
		if (windowSize.height() > screenSize.height() || windowSize.width() > screenSize.width())
			windowSize.scale(screenSize, Qt::KeepAspectRatio);
		resize(windowSize);
		update();
	}
}

QString CImageViewerWidget::imageInfoString() const
{
	if (_sourceImage.isNull())
		return QString();

	const int numChannels = _sourceImage.isGrayscale() ? 1 : (3 + (_sourceImage.hasAlphaChannel() ? 1 : 0));
	return QString("%1x%2, %3 channels, %4 bits per pixel, compressed to %5 bits per pixel").
			arg(_sourceImage.width()).
			arg(_sourceImage.height()).
			arg(numChannels).
			arg(_sourceImage.bitPlaneCount()).
			arg(8 * _imageFileSize / (double(_sourceImage.width()) * _sourceImage.height()));
}

QSize CImageViewerWidget::sizeHint() const
{
	return _sourceImage.size();
}

QIcon CImageViewerWidget::imageIcon(const QSize & size) const
{
	if (_sourceImage.isNull())
		return QIcon();
	return QIcon(QPixmap::fromImage(CImageResizer::resize(_sourceImage, size, CImageResizer::Bicubic)));
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	if (_scaledImage.isNull() || _scaledImage.size() != _sourceImage.size().scaled(size(), Qt::KeepAspectRatio))
		_scaledImage = CImageResizer::resize(_sourceImage, size(), CImageResizer::Bicubic);

	if (!_sourceImage.isNull())
		QPainter(this).drawImage(0, 0, _scaledImage);
}
