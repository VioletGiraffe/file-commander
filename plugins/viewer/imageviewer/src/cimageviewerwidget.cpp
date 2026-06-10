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
#include <QScreen>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <math.h>

namespace
{
	[[nodiscard]] inline QRectF centeredTargetRect(const QSize& targetSize, const QSize& viewportSize) noexcept
	{
		return QRectF{
			(viewportSize.width() - targetSize.width()) / 2.0,
			(viewportSize.height() - targetSize.height()) / 2.0,
			(qreal)targetSize.width(),
			(qreal)targetSize.height()
		};
	}

	[[nodiscard]] inline ImageProcessing::Rect toRect(const QRectF& r) noexcept
	{
		return {
			static_cast<uint64_t>(qRound64(r.x())),
			static_cast<uint64_t>(qRound64(r.y())),
			static_cast<uint64_t>(qRound64(r.width())),
			static_cast<uint64_t>(qRound64(r.height()))
		};
	}

	[[nodiscard]] QPointF visibleSpanUv(const QSize& sourceSize, const QSize& viewportSize, qreal zoom) noexcept
	{
		if (sourceSize.isEmpty() || viewportSize.isEmpty() || zoom <= 1.0)
			return { 1.0, 1.0 };

		const qreal sourceAspect = sourceSize.width() / (qreal)sourceSize.height();
		const qreal viewAspect = viewportSize.width() / (qreal)viewportSize.height();

		if (sourceAspect >= viewAspect)
			return { (viewAspect / sourceAspect) / zoom, 1.0 / zoom };
		else
			return { 1.0 / zoom, (sourceAspect / viewAspect) / zoom };
	}

	[[nodiscard]] QRect computeSourceRect(const QSize& sourceSize, const QSize& viewportSize, const QPointF& centerUv, const qreal zoom) noexcept
	{
		if (sourceSize.isEmpty() || viewportSize.isEmpty())
			return {};

		if (zoom <= 1.0)
			return QRect{ 0, 0, sourceSize.width(), sourceSize.height() };

		const qreal viewAspect = viewportSize.width() / (qreal)viewportSize.height();
		const qreal sourceAspect = sourceSize.width() / (qreal)sourceSize.height();

		qreal baseW = 0.0;
		qreal baseH = 0.0;

		if (sourceAspect >= viewAspect)
		{
			baseH = (qreal)sourceSize.height();
			baseW = baseH * viewAspect;
		}
		else
		{
			baseW = (qreal)sourceSize.width();
			baseH = baseW / viewAspect;
		}

		const int visW = std::max(1, qRound(baseW / zoom));
		const int visH = std::max(1, qRound((qreal)visW / viewAspect));

		const qreal centerPxX = centerUv.x() * sourceSize.width();
		const qreal centerPxY = centerUv.y() * sourceSize.height();

		const qreal halfW = visW / 2.0;
		const qreal halfH = visH / 2.0;

		const qreal cx = std::clamp(centerPxX, halfW, sourceSize.width() - halfW);
		const qreal cy = std::clamp(centerPxY, halfH, sourceSize.height() - halfH);

		int x = qRound(cx - halfW);
		int y = qRound(cy - halfH);
		x = std::clamp(x, 0, sourceSize.width() - visW);
		y = std::clamp(y, 0, sourceSize.height() - visH);

		return QRect{ x, y, visW, visH };
	}

	[[nodiscard]] QPointF sourcePointUnderWidgetPos(const QPointF& widgetPos, const QRectF& targetRect, const QRect& srcRect) noexcept
	{
		if (targetRect.width() <= 0.0 || targetRect.height() <= 0.0)
			return QPointF{ (qreal)srcRect.center().x(), (qreal)srcRect.center().y() };

		const qreal u = std::clamp((widgetPos.x() - targetRect.x()) / targetRect.width(), 0.0, 1.0);
		const qreal v = std::clamp((widgetPos.y() - targetRect.y()) / targetRect.height(), 0.0, 1.0);

		return QPointF{
			srcRect.x() + u * srcRect.width(),
			srcRect.y() + v * srcRect.height()
		};
	}
}

template <bool ConstView>
inline ImageProcessing::ImageView<ConstView> createView(const QImage& qi)
{
	ImageProcessing::ImageView<ConstView> view;
	view.width = static_cast<uint64_t>(qi.width());
	view.height = static_cast<uint64_t>(qi.height());

	const auto format = qi.format();

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

	if (qi.format() == QImage::Format_RGB32)
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
	_zoom = 1.0;
	_imageCenterUv = QPointF{ 0.5, 0.5 };
	_isPanning = false;
	_cacheKey = 0;
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

	const QSize fitSize = _sourceImage.size().scaled(size(), Qt::KeepAspectRatio);
	const qreal zoom = std::clamp(_zoom, 0.01, 40.0);

	QSize targetSize;
	QRect sourceRect;

	if (zoom <= 1.0)
	{
		targetSize = QSize{
			std::max(1, qRound(fitSize.width() * zoom)),
			std::max(1, qRound(fitSize.height() * zoom))
		};
		sourceRect = QRect{ 0, 0, _sourceImage.width(), _sourceImage.height() };
	}
	else
	{
		targetSize = size();
		sourceRect = computeSourceRect(_sourceImage.size(), size(), _imageCenterUv, zoom);
	}

	if (_displayImage.size() != targetSize || _displayImage.format() != _sourceImage.format())
		_displayImage = QImage(targetSize.width(), targetSize.height(), _sourceImage.format());

	_displayImage.setDevicePixelRatio(devicePixelRatioF());

	const size_t newCacheKey = qHash(sourceRect) ^ qHash(targetSize);
	if (newCacheKey != _cacheKey)
	{
		_cacheKey = newCacheKey;

		try {
			const auto srcView = createView<true>(_sourceImage);
			auto dstView = createView<false>(_displayImage);
			ImageProcessing::resize(dstView, srcView, toRect(sourceRect));
		}
		catch (...) {
			_displayImage = _sourceImage.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			_displayImage.setDevicePixelRatio(devicePixelRatioF());
		}
	}

	if (zoom <= 1.0)
	{
		const QRectF targetRect = centeredTargetRect(targetSize, size());
		p.drawImage(targetRect, _displayImage);
	}
	else
	{
		p.drawImage(0, 0, _displayImage);
	}
}

void CImageViewerWidget::wheelEvent(QWheelEvent* e)
{
	if (_sourceImage.isNull() || !e->modifiers().testFlag(Qt::ControlModifier))
		return;

	const int delta = e->angleDelta().y();
	if (delta == 0)
		return;

	const QSize fitSize = _sourceImage.size().scaled(size(), Qt::KeepAspectRatio);
	const qreal oldZoom = std::clamp(_zoom, 0.01, 40.0);
	const QPointF anchorWidgetPos = e->position();

	QSize oldTargetSize;
	QRect oldSourceRect;

	if (oldZoom <= 1.0)
	{
		oldTargetSize = QSize{
			std::max(1, qRound(fitSize.width() * oldZoom)),
			std::max(1, qRound(fitSize.height() * oldZoom))
		};

		oldSourceRect = QRect{ 0, 0, _sourceImage.width(), _sourceImage.height() };
	}
	else
	{
		oldTargetSize = size();
		oldSourceRect = computeSourceRect(_sourceImage.size(), size(), _imageCenterUv, oldZoom);
	}

	const QRectF oldTargetRect = centeredTargetRect(oldTargetSize, size());

	const QPointF anchorSourcePoint = sourcePointUnderWidgetPos(
		anchorWidgetPos,
		oldTargetRect,
		oldSourceRect);

	const qreal anchorU =
		oldTargetRect.width() > 0.0 ?
		(anchorWidgetPos.x() - oldTargetRect.x()) / oldTargetRect.width()
		:
		0.5;

	const qreal anchorV =
		oldTargetRect.height() > 0.0 ?
		(anchorWidgetPos.y() - oldTargetRect.y()) / oldTargetRect.height()
		:
		0.5;

	const qreal scaleFactor = std::pow(1.0015, (qreal)delta);
	_zoom = std::clamp(oldZoom * scaleFactor, 0.01, 40.0);

	if (_zoom > 1.0)
	{
		const QPointF spanUv = visibleSpanUv(_sourceImage.size(), size(), _zoom);

		_imageCenterUv = QPointF{
			anchorSourcePoint.x() / _sourceImage.width() + (0.5 - anchorU) * spanUv.x(),
			anchorSourcePoint.y() / _sourceImage.height() + (0.5 - anchorV) * spanUv.y()
		};

		const qreal halfSpanX = spanUv.x() * 0.5;
		const qreal halfSpanY = spanUv.y() * 0.5;

		_imageCenterUv.setX(std::clamp(_imageCenterUv.x(), halfSpanX, 1.0 - halfSpanX));
		_imageCenterUv.setY(std::clamp(_imageCenterUv.y(), halfSpanY, 1.0 - halfSpanY));
	}

	update();
	e->accept();
}

void CImageViewerWidget::mousePressEvent(QMouseEvent* e)
{
	if (_sourceImage.isNull() || e->button() != Qt::LeftButton || _zoom <= 1.0)
	{
		QWidget::mousePressEvent(e);
		return;
	}

	_isPanning = true;
	_panStartMousePos = e->position().toPoint();
	_panStartCenterUv = _imageCenterUv;
	setCursor(Qt::ClosedHandCursor);
	e->accept();
}

void CImageViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
	if (!_isPanning || _sourceImage.isNull() || _zoom <= 1.0)
	{
		QWidget::mouseMoveEvent(e);
		return;
	}

	const QSize viewportSize = size();

	if (viewportSize.isEmpty())
	{
		e->accept();
		return;
	}

	const QPoint delta = e->position().toPoint() - _panStartMousePos;
	const QPointF spanUv = visibleSpanUv(_sourceImage.size(), viewportSize, _zoom);

	QPointF newCenterUv{
		_panStartCenterUv.x() - delta.x() * spanUv.x() / viewportSize.width(),
		_panStartCenterUv.y() - delta.y() * spanUv.y() / viewportSize.height()
	};

	const qreal halfSpanX = spanUv.x() * 0.5;
	const qreal halfSpanY = spanUv.y() * 0.5;

	newCenterUv.setX(std::clamp(newCenterUv.x(), halfSpanX, 1.0 - halfSpanX));
	newCenterUv.setY(std::clamp(newCenterUv.y(), halfSpanY, 1.0 - halfSpanY));

	if (newCenterUv != _imageCenterUv)
	{
		_imageCenterUv = newCenterUv;
		update();
	}

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