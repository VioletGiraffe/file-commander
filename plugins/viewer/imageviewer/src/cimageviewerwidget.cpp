#include "cimageviewerwidget.h"
#include "resize/cimageresizer.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QImageReader>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QtMath>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <math.h>

namespace
{
	// Position of one axis of the image within the viewport, in device pixels: center it when it is smaller than the
	// viewport (letterbox), otherwise keep its edges inside (crop/pan). Applied per axis, this is what lets a single
	// view letterbox one axis while cropping the other.
	[[nodiscard]] inline qreal clampAxis(qreal offset, qreal imageLength, qreal viewportLength) noexcept
	{
		if (imageLength <= viewportLength)
			return (viewportLength - imageLength) / 2.0;

		return std::clamp(offset, viewportLength - imageLength, 0.0);
	}

	[[nodiscard]] inline ImageProcessing::Rect toRect(const QRect& r) noexcept
	{
		return {
			static_cast<uint64_t>(r.x()),
			static_cast<uint64_t>(r.y()),
			static_cast<uint64_t>(r.width()),
			static_cast<uint64_t>(r.height())
		};
	}

	constexpr qreal kMaxScale = 40.0; // device px per source px; the maximum magnification.
}

template <bool ConstView>
inline ImageProcessing::ImageView<ConstView> createView(const QImage& qi)
{
	ImageProcessing::ImageView<ConstView> view;
	view.width = static_cast<uint64_t>(qi.width());
	view.height = static_cast<uint64_t>(qi.height());

	const auto format = qi.format();

	switch (format)
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
	case QImage::Format_RGBA64:
		view.channels = 4;
		view.bytesPerChannel = 2;
		break;
	case QImage::Format_RGBX64:
		view.channels = 3;
		view.bytesPerChannel = 2;
		break;
	default:
		throw std::runtime_error{ "Unsupported QImage format" };
	}

	if (format == QImage::Format_RGB32)
		view.channelStride = 4;
	else
		view.channelStride = view.channels * view.bytesPerChannel;

	view.bytesPerLine = static_cast<size_t>(qi.bytesPerLine());
	view.data = const_cast<uchar*>(qi.bits());

	assert_r(view.bytesPerLine >= view.width * view.channelStride);

	return view;
}

bool CImageViewerWidget::displayImage(const QImage& image)
{
	_sourceImage = image;
	_isPanning = false;
	_cacheKey = 0;
	_viewInitialized = false; // Refit to the new image on the next paint.
	updateGeometry(); // Because the image affects sizeHint()
	update();
	return !_sourceImage.isNull();
}

bool CImageViewerWidget::displayImage(const QString& imagePath)
{
	QImageReader reader(imagePath);
	reader.setAutoDetectImageFormat(true);
	reader.setAutoTransform(true);

	_currentImageFormat = QString::fromLatin1(reader.format());
	QImage img = reader.read();
	if (img.isNull())
	{
		_currentImageFileSize = 0;
		_currentImageFormat.clear();
		QMessageBox::warning(parentWidget(), tr("Failed to load the image"), tr("Failed to load the image %1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
		return false;
	}

	if (img.format() == QImage::Format_Indexed8 || img.format() == QImage::Format_Grayscale16 || img.format() == QImage::Format_RGBA64 || img.format() == QImage::Format_RGBX64)
	{
		img.convertTo(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
	}

	_currentImageFileSize = reader.device()->size();
	return displayImage(img);
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
	return QSize{
		std::clamp(qCeil(_sourceImage.width() / dpr), 150, maxSize.width()),
		std::clamp(qCeil(_sourceImage.height() / dpr), 150, maxSize.height())
	};
}

QIcon CImageViewerWidget::imageIcon(const std::vector<QSize>& sizes) const
{
	QIcon result;

	try {
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
	} catch (...) {}

	return result;
}

void CImageViewerWidget::copyToClipboard() noexcept
{
	if (!_sourceImage.isNull())
		QApplication::clipboard()->setImage(_sourceImage);
}

void CImageViewerWidget::copyDisplayedToClipboard() noexcept
{
	if (!_displayImage.isNull())
		QApplication::clipboard()->setImage(_displayImage);
}

qreal CImageViewerWidget::fitScale(const QSize& viewportLogicalSize) const noexcept
{
	const qreal dpr = devicePixelRatioF();
	return std::min(viewportLogicalSize.width() * dpr / _sourceImage.width(),
	                viewportLogicalSize.height() * dpr / _sourceImage.height());
}

qreal CImageViewerWidget::minScale() const noexcept
{
	// Zoom out until the whole image fits, but never below 1:1 so a small image can still be shown at native size.
	return std::min(fitScale(size()), 1.0);
}

QPointF CImageViewerWidget::centeredOffset() const noexcept
{
	const qreal dpr = devicePixelRatioF();
	return QPointF{
		(width() * dpr - _sourceImage.width() * _scale) / 2.0,
		(height() * dpr - _sourceImage.height() * _scale) / 2.0
	};
}

bool CImageViewerWidget::isPannable() const noexcept
{
	const qreal dpr = devicePixelRatioF();
	return _sourceImage.width() * _scale > width() * dpr + 0.5
		|| _sourceImage.height() * _scale > height() * dpr + 0.5;
}

void CImageViewerWidget::clampOffset() noexcept
{
	const qreal dpr = devicePixelRatioF();
	_offset.setX(clampAxis(_offset.x(), _sourceImage.width() * _scale, width() * dpr));
	_offset.setY(clampAxis(_offset.y(), _sourceImage.height() * _scale, height() * dpr));
}

void CImageViewerWidget::resetToFit() noexcept
{
	_scale = fitScale(size());
	_offset = centeredOffset();
	_viewInitialized = true;
}

void CImageViewerWidget::fitToWindow() noexcept
{
	if (_sourceImage.isNull() || size().isEmpty())
		return;

	resetToFit();
	update();
}

void CImageViewerWidget::zoomToActualPixels() noexcept
{
	if (_sourceImage.isNull() || size().isEmpty())
		return;

	_scale = 1.0; // 1:1 == one source pixel per one device pixel; always within [minScale(), kMaxScale].
	_offset = centeredOffset();
	clampOffset();
	_viewInitialized = true;
	update();
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	QPainter p{ this };
	p.fillRect(rect(), palette().color(QPalette::Window));

	if (_sourceImage.isNull())
	{
		QFont bigFont = font();
		bigFont.setPointSize(28);
		p.setFont(bigFont);
		p.drawText(rect(), Qt::AlignCenter, tr("No image loaded"));
		return;
	}

	if (size().isEmpty())
		return;

	if (!_viewInitialized)
		resetToFit();

	const qreal dpr = devicePixelRatioF();
	const qreal viewW = width() * dpr;
	const qreal viewH = height() * dpr;

	// Visible source rectangle: back-project the viewport through the inverse transform sourcePx = (devicePx - offset) / scale, clipped to the image.
	const int srcX0 = qFloor(std::clamp(-_offset.x() / _scale, 0.0, (qreal)_sourceImage.width()));
	const int srcY0 = qFloor(std::clamp(-_offset.y() / _scale, 0.0, (qreal)_sourceImage.height()));
	const int srcX1 = qCeil(std::clamp((viewW - _offset.x()) / _scale, 0.0, (qreal)_sourceImage.width()));
	const int srcY1 = qCeil(std::clamp((viewH - _offset.y()) / _scale, 0.0, (qreal)_sourceImage.height()));

	const QRect sourceRect{ srcX0, srcY0, srcX1 - srcX0, srcY1 - srcY0 };
	if (sourceRect.isEmpty())
		return;

	// The visible crop occupies sourceRect * scale device pixels; render the buffer at exactly that resolution for a 1:1 blit.
	const QSize bufferPx{
		std::max(1, qRound(sourceRect.width() * _scale)),
		std::max(1, qRound(sourceRect.height() * _scale))
	};

	if (_displayImage.size() != bufferPx || _displayImage.format() != _sourceImage.format())
		_displayImage = QImage(bufferPx.width(), bufferPx.height(), _sourceImage.format());

	_displayImage.setDevicePixelRatio(dpr);

	const size_t newCacheKey = qHash(sourceRect) ^ qHash(bufferPx);
	if (newCacheKey != _cacheKey)
	{
		_cacheKey = newCacheKey;

		try {
			const auto srcView = createView<true>(_sourceImage);
			auto dstView = createView<false>(_displayImage);
			ImageProcessing::resize(dstView, srcView, toRect(sourceRect));
		}
		catch (...) {
			_displayImage = _sourceImage.copy(sourceRect).scaled(bufferPx, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			_displayImage.setDevicePixelRatio(dpr);
		}

		emit displayedSizeChanged(_displayImage.size(), _scale); // _scale is the magnification (source px : device px).
	}

	// Snap the blit to a whole device pixel so a 1:1 view stays pixel-exact; a fractional origin would make the painter resample the buffer.
	const QPoint targetDevice = (_offset + QPointF{ (qreal)sourceRect.x(), (qreal)sourceRect.y() } * _scale).toPoint();
	p.drawImage(QPointF{ targetDevice } / dpr, _displayImage);
}

void CImageViewerWidget::resizeEvent(QResizeEvent* e)
{
	QWidget::resizeEvent(e);

	if (_sourceImage.isNull() || !_viewInitialized || !e->oldSize().isValid() || size().isEmpty())
		return;

	// A view that showed the whole image keeps fitting it; a zoomed-in view keeps its scale and the source point at the viewport center.
	if (_scale <= fitScale(e->oldSize()) * (1.0 + 1e-3))
	{
		resetToFit();
		return;
	}

	const qreal dpr = devicePixelRatioF();
	const QPointF centerSource = (QPointF{ e->oldSize().width() * dpr / 2.0, e->oldSize().height() * dpr / 2.0 } - _offset) / _scale;

	_scale = std::clamp(_scale, minScale(), kMaxScale);
	_offset = QPointF{ width() * dpr / 2.0, height() * dpr / 2.0 } - centerSource * _scale;
	clampOffset();
}

void CImageViewerWidget::wheelEvent(QWheelEvent* e)
{
	if (_sourceImage.isNull() || !_viewInitialized || size().isEmpty() || !e->modifiers().testFlag(Qt::ControlModifier))
		return;

	const int delta = e->angleDelta().y();
	if (delta == 0)
		return;

	const qreal dpr = devicePixelRatioF();
	const QPointF cursorDevice = e->position() * dpr;
	const qreal newScale = std::clamp(_scale * std::pow(1.0015, (qreal)delta), minScale(), kMaxScale);

	if (newScale != _scale)
	{
		// Keep the source pixel under the cursor pinned in place as the scale changes.
		_offset = cursorDevice - (cursorDevice - _offset) * (newScale / _scale);
		_scale = newScale;
		clampOffset();
		update();
	}

	e->accept();
}

void CImageViewerWidget::mousePressEvent(QMouseEvent* e)
{
	if (_sourceImage.isNull() || !_viewInitialized || e->button() != Qt::LeftButton || !isPannable())
	{
		QWidget::mousePressEvent(e);
		return;
	}

	_isPanning = true;
	_panStartOffset = _offset;
	_panStartMouseDevice = e->position() * devicePixelRatioF();
	setCursor(Qt::ClosedHandCursor);
	e->accept();
}

void CImageViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
	if (!_isPanning || _sourceImage.isNull())
	{
		QWidget::mouseMoveEvent(e);
		return;
	}

	_offset = _panStartOffset + (e->position() * devicePixelRatioF() - _panStartMouseDevice);
	clampOffset();
	update();
	e->accept();
}

void CImageViewerWidget::mouseReleaseEvent(QMouseEvent* e)
{
	if (e->button() == Qt::LeftButton && _isPanning)
	{
		_isPanning = false;
		unsetCursor();
		e->accept();
		return;
	}

	QWidget::mouseReleaseEvent(e);
}