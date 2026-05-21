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

	[[nodiscard]] QRect computeSourceRect(const QSize& sourceSize, const QPointF& centerPx, const qreal zoom) noexcept
	{
		if (sourceSize.isEmpty())
			return {};

		if (zoom <= 1.0)
			return QRect{ 0, 0, sourceSize.width(), sourceSize.height() };

		const int visW = std::clamp(std::max(1, qRound(sourceSize.width() / zoom)), 1, sourceSize.width());
		const int visH = std::clamp(std::max(1, qRound(sourceSize.height() / zoom)), 1, sourceSize.height());

		const qreal halfW = visW / 2.0;
		const qreal halfH = visH / 2.0;

		qreal cx = std::clamp(centerPx.x(), halfW, sourceSize.width() - halfW);
		qreal cy = std::clamp(centerPx.y(), halfH, sourceSize.height() - halfH);

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

	[[nodiscard]] inline QPointF centerForAnchorPreservation(const QPointF& anchorSourcePoint, const QPointF& anchorUV, const QSize& visibleSrcSize) noexcept
	{
		return QPointF{
			anchorSourcePoint.x() + (0.5 - anchorUV.x()) * visibleSrcSize.width(),
			anchorSourcePoint.y() + (0.5 - anchorUV.y()) * visibleSrcSize.height()
		};
	}
}

template <bool ConstView>
inline ImageProcessing::ImageView<ConstView> createView(const QImage& qi)
{
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

CImageViewerWidget::CImageViewerWidget(QWidget* parent) noexcept :
	QWidget(parent)
{
}

bool CImageViewerWidget::displayImage(const QImage& image)
{
	_sourceImage = image;
	_zoom = 1.0;
	_zoomCenter = QPoint{};
	_imageCenterPx = _sourceImage.isNull() ?
		QPointF{ 0.0, 0.0 }
		:
		QPointF{ (qreal)_sourceImage.width() / 2.0, (qreal)_sourceImage.height() / 2.0 };
	_isPanning = false;
	update();
	return !_sourceImage.isNull();
}

bool CImageViewerWidget::displayImage(const QString& imagePath)
{
	QImageReader reader(imagePath);
	reader.setAutoDetectImageFormat(true);
	reader.setAutoTransform(true);

	_currentImageFormat = QString::fromLatin1(reader.format());
	const QImage img = reader.read();
	if (img.isNull())
	{
		_currentImageFileSize = 0;
		_currentImageFormat.clear();
		QMessageBox::warning(parentWidget(), tr("Failed to load the image"), tr("Failed to load the image %1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
		return false;
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
		targetSize = fitSize;
		sourceRect = computeSourceRect(_sourceImage.size(), _imageCenterPx, zoom);
	}

	const QRectF targetRect = centeredTargetRect(targetSize, size());
	p.drawImage(targetRect, _sourceImage, sourceRect);
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
		oldTargetSize = fitSize;
		oldSourceRect = computeSourceRect(_sourceImage.size(), _imageCenterPx, oldZoom);
	}

	const QRectF oldTargetRect = centeredTargetRect(oldTargetSize, size());
	const QPointF anchorSourcePoint = sourcePointUnderWidgetPos(anchorWidgetPos, oldTargetRect, oldSourceRect);
	const QPointF anchorUV {
		oldTargetRect.width() > 0.0 ? (anchorWidgetPos.x() - oldTargetRect.x()) / oldTargetRect.width() : 0.5,
		oldTargetRect.height() > 0.0 ? (anchorWidgetPos.y() - oldTargetRect.y()) / oldTargetRect.height() : 0.5
	};

	const qreal scaleFactor = std::pow(1.0015, (qreal)delta);
	const qreal newZoom = std::clamp(oldZoom * scaleFactor, 0.01, 40.0);
	_zoom = newZoom;
	_zoomCenter = anchorWidgetPos.toPoint();

	if (newZoom > 1.0)
	{
		const QSize newVisibleSrcSize{
			std::clamp(std::max(1, qRound(_sourceImage.width() / newZoom)), 1, _sourceImage.width()),
			std::clamp(std::max(1, qRound(_sourceImage.height() / newZoom)), 1, _sourceImage.height())
		};

		QPointF newCenterPx = centerForAnchorPreservation(anchorSourcePoint, anchorUV, newVisibleSrcSize);
		newCenterPx.setX(std::clamp(newCenterPx.x(), newVisibleSrcSize.width() / 2.0, _sourceImage.width() - newVisibleSrcSize.width() / 2.0));
		newCenterPx.setY(std::clamp(newCenterPx.y(), newVisibleSrcSize.height() / 2.0, _sourceImage.height() - newVisibleSrcSize.height() / 2.0));
		_imageCenterPx = newCenterPx;
	}
	else
	{
		// Keep the view centered on the image while zooming out.
		_imageCenterPx = QPointF{ _sourceImage.width() / 2.0, _sourceImage.height() / 2.0 };
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
	_panStartCenterPx = _imageCenterPx;
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

	const QSize fitSize = _sourceImage.size().scaled(size(), Qt::KeepAspectRatio);
	const qreal zoom = std::clamp(_zoom, 0.01, 40.0);
	const QSize targetSize = fitSize;
	const QSize visibleSrcSize{
		std::clamp(std::max(1, qRound(_sourceImage.width() / zoom)), 1, _sourceImage.width()),
		std::clamp(std::max(1, qRound(_sourceImage.height() / zoom)), 1, _sourceImage.height())
	};

	if (targetSize.isEmpty())
	{
		e->accept();
		return;
	}

	const QPoint delta = e->position().toPoint() - _panStartMousePos;
	const qreal srcPerWidgetX = visibleSrcSize.width() / (qreal)targetSize.width();
	const qreal srcPerWidgetY = visibleSrcSize.height() / (qreal)targetSize.height();

	QPointF newCenterPx{
		_panStartCenterPx.x() - delta.x() * srcPerWidgetX,
		_panStartCenterPx.y() - delta.y() * srcPerWidgetY
	};
	newCenterPx.setX(std::clamp(newCenterPx.x(), visibleSrcSize.width() / 2.0, _sourceImage.width() - visibleSrcSize.width() / 2.0));
	newCenterPx.setY(std::clamp(newCenterPx.y(), visibleSrcSize.height() / 2.0, _sourceImage.height() - visibleSrcSize.height() / 2.0));

	if (newCenterPx != _imageCenterPx)
	{
		_imageCenterPx = newCenterPx;
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
