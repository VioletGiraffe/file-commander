#include "cimageviewerwidget.h"
#include "widgets/widgetutils.h"
#include "imageprocessing/resize/cimageresizer.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QImageReader>
#include <QtMath>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
RESTORE_COMPILER_WARNINGS

#include <algorithm> // clamp

CImageViewerWidget::CImageViewerWidget(QWidget *parent) noexcept :
	QWidget(parent)
{
}

bool CImageViewerWidget::displayImage(const QImage& image)
{
	_sourceImage = image;
	if (image.isNull())
		return false;

	update();
	return true;
}

bool CImageViewerWidget::displayImage(const QString& imagePath)
{
	QImageReader reader(imagePath);
	reader.setAutoDetectImageFormat(true);
	reader.setAutoTransform(true);

	_currentImageFormat = QString::fromLatin1(reader.format()); // Must be called before read()
	QImage img = reader.read();
	if (!img.isNull())
	{
		_currentImageFileSize = reader.device()->size();
		return displayImage(img);
	}

	_currentImageFileSize = 0;
	_currentImageFormat.clear();

	QMessageBox::warning(parentWidget(), tr("Failed to load the image"), tr("Failed to load the image\n\n%1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
	return false;
}

QString CImageViewerWidget::imageInfoString() const
{
	if (_sourceImage.isNull())
		return QString();

	const int numChannels = _sourceImage.isGrayscale() ? 1 : (3 + (_sourceImage.hasAlphaChannel() ? 1 : 0));
	return _currentImageFormat.toUpper() + ' ' + tr("%1x%2 (%3 MP), %4 channels, %5 bits per pixel, compressed to %6 bits per pixel").
		arg(_sourceImage.width()).
		arg(_sourceImage.height()).
		arg(_sourceImage.width() * _sourceImage.height() * 1e-6, 0, 'f', 1).
		arg(numChannels).
		arg(_sourceImage.bitPlaneCount()).
		arg(QString::number(8.0 * (double)_currentImageFileSize / double(_sourceImage.width() * _sourceImage.height()), 'f', 2));
}

QSize CImageViewerWidget::sizeHint() const
{
	const auto maxSize = screen()->availableGeometry().size() - QSize(60, 60);
	if (_sourceImage.isNull())
		return QWidget::sizeHint();

	const qreal dpr = devicePixelRatioF();
	auto hint = QSize{
		std::clamp(qCeil(_sourceImage.width() / dpr),  150, maxSize.width()),
		std::clamp(qCeil(_sourceImage.height() / dpr), 150, maxSize.height())
	};
	return hint;
}

QIcon CImageViewerWidget::imageIcon(const std::vector<QSize>& sizes) const
{
	QIcon result;
	if (!_sourceImage.isNull())
	{
		for (const auto& s : sizes)
			result.addPixmap(QPixmap::fromImage(ImageResizing::resize(_sourceImage, s, ImageResizing::Smart)));
	}

	return result;
}

void CImageViewerWidget::copyToClipboard() noexcept
{
	if (!_sourceImage.isNull())
		QApplication::clipboard()->setImage(_sourceImage);
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	QPainter p{ this };
	if (_sourceImage.isNull())
	{
		p.fillRect(rect(), palette().color(QPalette::Window));
		QFont bigFont = font();
		bigFont.setPointSize(bigFont.pointSize() * 2);
		p.setFont(bigFont);
		p.drawText(rect(), Qt::AlignCenter, tr("No image loaded"));
		return;
	}

	const qreal dpr = devicePixelRatioF();

	const QSize scaledSize = _sourceImage.size().scaled(size() * dpr, Qt::KeepAspectRatio);
	// Check if the scaled size is withing +/-3 pixels of the source image size, and display the source image pixel-perfect in that case
	if (qAbs(scaledSize.width() - _sourceImage.width()) <= 3 &&
		qAbs(scaledSize.height() - _sourceImage.height()) <= 3)
	{
		_scaledImage = _sourceImage;
	}
	else if (_scaledImage.isNull() || _scaledImage.size() != scaledSize)
	{
		_scaledImage = ImageResizing::resize(_sourceImage, scaledSize, ImageResizing::Smart);
	}

	_scaledImage.setDevicePixelRatio(devicePixelRatioF());
	p.drawImage(0, 0, _scaledImage);
}
