#include "cimageviewerwidget.h"
#include "widgets/widgetutils.h"
#include "resize/cimageresizer.h"
#include "assert/advanced_assert.h"
#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QImageReader>
#include <QtMath>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <algorithm> // clamp

template <bool ConstView>
inline ImageProcessing::ImageView<ConstView> createView(const QImage& qi) {
	ImageProcessing::ImageView<ConstView> view;
	view.width = static_cast<uint32_t>(qi.width());
	view.height = static_cast<uint32_t>(qi.height());

	switch (qi.format())
	{
	case QImage::Format_Grayscale8: [[fallthrough]];
	case QImage::Format_Indexed8:
		view.channels = 1;
		view.bytesPerChannel = 1;
		break;
	case QImage::Format_Grayscale16:
		view.channels = 1;
		view.bytesPerChannel = 2;
		break;
	case QImage::Format_RGB888: [[fallthrough]];
	case QImage::Format_RGB32:
		view.channels = 3;
		view.bytesPerChannel = 1;
		break;
	case QImage::Format_ARGB32: [[fallthrough]];
	case QImage::Format_ARGB32_Premultiplied: [[fallthrough]];
	case QImage::Format_RGBA8888: [[fallthrough]];
	case QImage::Format_RGBA8888_Premultiplied:
		view.channels = 4;
		view.bytesPerChannel = 1;
		break;
	default:
		throw std::runtime_error{ "Unsupported QImage format" };
	}

	if (qi.format() == QImage::Format_RGB32)
		view.channelStride = 4;
	else
		view.channelStride = view.channels * view.bytesPerChannel;

	view.bytesPerLine = static_cast<size_t>(qi.bytesPerLine());
	view.data = const_cast<uchar*>(qi.bits());

	assert_r(view.bytesPerLine >= view.width * view.channelStride);

	return view;
}

CImageViewerWidget::CImageViewerWidget(QWidget *parent) noexcept :
	QWidget(parent)
{
}

bool CImageViewerWidget::displayImage(const QImage& image)
{
	_sourceImage = image;
	update();
	return !_sourceImage.isNull();
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
	if (_sourceImage.isNull())
		return QWidget::sizeHint();

	const auto maxSize = screen()->availableGeometry().size() - QSize(60, 60);
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
		{
			QImage scaledImage(s.width(), s.height(), _sourceImage.format());
			scaledImage.fill(Qt::green);

			const auto srcView = createView<true>(_sourceImage);
			auto dstView = createView<false>(scaledImage);
			ImageProcessing::resize(dstView, srcView);

			result.addPixmap(QPixmap::fromImage(scaledImage));
		}
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
	// Clear the canvas - in case the new image size is smaller than previous paint, or if the image was not loaded, or the image has transparency
	p.fillRect(rect(), palette().color(QPalette::Window));

	if (_sourceImage.isNull())
	{
		QFont bigFont = font();
		bigFont.setPointSize(28);
		p.setFont(bigFont);
		p.drawText(rect(), Qt::AlignCenter, tr("No image loaded"));
		return;
	}

	const qreal dpr = devicePixelRatioF();

	const QSize scaledSize = _sourceImage.size().scaled(size() * dpr * _zoom, Qt::KeepAspectRatio);
	// Check if the scaled size is within +/-3 pixels of the source image size, and display the source image pixel-perfect in that case
	if (qAbs(scaledSize.width() - _sourceImage.width()) <= 3 &&
		qAbs(scaledSize.height() - _sourceImage.height()) <= 3)
	{
		_scaledImage = _sourceImage;
	}
	else if (_scaledImage.isNull() || _scaledImage.size() != scaledSize)
	{
		// Calculate the source images rect
		ImageProcessing::Rect srcRect(0, 0, scaledSize.width(), scaledSize.height());

		_scaledImage = QImage(scaledSize, _sourceImage.format());
		_scaledImage.fill(Qt::green);

		const auto srcView = createView<true>(_sourceImage);
		auto dstView = createView<false>(_scaledImage);
		CTimeElapsed timer(true);

		ImageProcessing::resize(dstView, srcView, srcRect);
		qInfo() << "Resizing from" << _sourceImage.size() << "to" << scaledSize << "took" << timer.elapsed() << "ms";
	}

	_scaledImage.setDevicePixelRatio(dpr);

	// If the image is smaller than viewport, center it
	QPoint offset{ 0, 0 };
	if (_scaledImage.width() / dpr < width() && _scaledImage.height() / dpr < height())
	{
		offset = QPoint{
			(width()  - qRound((qreal)_scaledImage.width() / dpr)) / 2,
			(height() - qRound((qreal)_scaledImage.height() / dpr)) / 2
		};
	}

	p.drawImage(offset, _scaledImage);
}

void CImageViewerWidget::wheelEvent(QWheelEvent* e)
{
	// Changing the scale with Ctrl + mouse wheel
	if (_sourceImage.isNull() || !e->modifiers().testFlag(Qt::ControlModifier))
		return;

	const int delta = e->angleDelta().y();
	if (delta == 0)
		return;

	const float scaleFactor = std::pow(1.0015f, (float)delta);
	_zoom = std::clamp(_zoom * scaleFactor, 0.01f, 40.0f);
	_zoomCenter = e->position().toPoint();

	update();
}
