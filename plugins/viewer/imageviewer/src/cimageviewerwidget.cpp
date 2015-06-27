#include "cimageviewerwidget.h"
#include "../../qtutils/imageprocessing/resize/cimageresizer.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDesktopWidget>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPainter>
RESTORE_COMPILER_WARNINGS

CImageViewerWidget::CImageViewerWidget(QWidget *parent) :
	QWidget(parent),
	_imageFileSize(0)
{
}

bool CImageViewerWidget::displayImage(const QString& imagePath, const QImage& image)
{
	_imageFileSize = QFileInfo(imagePath).size();
	_sourceImage = image.isNull() ? QImageReader(imagePath).read() : image;
	if (!_sourceImage.isNull())
	{
		QSize screenSize = QApplication::desktop()->availableGeometry().size() - QSize(50, 50);
		QSize windowSize = _sourceImage.size();
		if (windowSize.height() > screenSize.height() || windowSize.width() > screenSize.width())
			windowSize.scale(screenSize, Qt::KeepAspectRatio);
		resize(windowSize);
		update();

		return true;
	}
	else
	{
		QMessageBox::warning(dynamic_cast<QWidget*>(parent()), "Failed to load the image", QString("Failed to load the image\n\n") + imagePath + "\n\nIt is inaccessible, doesn't exist or is not a supported image file.");
		return false;
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
			arg(QString::number(8 * _imageFileSize / (double(_sourceImage.width()) * _sourceImage.height()), 'f', 2));
}

QSize CImageViewerWidget::sizeHint() const
{
	return _sourceImage.size();
}

QIcon CImageViewerWidget::imageIcon(const QList<QSize>& sizes) const
{
	QIcon result;
	if (!_sourceImage.isNull())
		for(QSize s: sizes)
			result.addPixmap(QPixmap::fromImage(CImageResizer::resize(_sourceImage, s, CImageResizer::Smart)));

	return result;
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	if (_scaledImage.isNull() || _scaledImage.size() != _sourceImage.size().scaled(size(), Qt::KeepAspectRatio))
		_scaledImage = CImageResizer::resize(_sourceImage, size(), CImageResizer::Smart);

	if (!_sourceImage.isNull())
		QPainter(this).drawImage(0, 0, _scaledImage);
}
