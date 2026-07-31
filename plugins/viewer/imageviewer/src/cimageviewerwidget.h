#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QImage>
#include <QSize>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <vector>

class CImageViewerWidget final : public QWidget
{
	Q_OBJECT

public:
	using QWidget::QWidget;

public:
	bool displayImage(const QImage& image);
	bool displayImage(const QString& imagePath);
	[[nodiscard]] const QImage& sourceImage() const noexcept { return _sourceImage; }
	[[nodiscard]] QString imageInfoString() const;

	[[nodiscard]] QSize sizeHint() const override;

	[[nodiscard]] QIcon imageIcon(const std::vector<QSize>& sizes) const;

	void copyToClipboard() noexcept;
	void copyDisplayedToClipboard() noexcept;

	void fitToWindow() noexcept;
	void zoomToActualPixels() noexcept;

signals:
	// magnification is the ratio of on-screen pixels to source-image pixels, i.e. 1.0 = native resolution ("100%")
	void displayedSizeChanged(QSize size, qreal magnification);

protected:
	void paintEvent(QPaintEvent* e) override;
	void resizeEvent(QResizeEvent* e) override;
	void wheelEvent(QWheelEvent* e) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;

private:
	// The view is an affine map from source pixels to viewport device pixels: devicePos = _offset + sourcePos * _scale.
	[[nodiscard]] QSizeF viewportDeviceSize() const noexcept;
	[[nodiscard]] QSizeF scaledImageSize() const noexcept;                         // on-screen size of the whole image, device px
	[[nodiscard]] QRect visibleSourceRect() const noexcept;                        // source pixels the viewport shows, outset to whole pixels
	[[nodiscard]] qreal fitScale() const noexcept;                                 // scale that fits the whole image
	[[nodiscard]] qreal minScale() const noexcept;                                 // most zoomed-out scale allowed
	[[nodiscard]] QPointF centeredOffset() const noexcept;                         // offset that centers the image at _scale
	[[nodiscard]] bool isPannable() const noexcept;                                // image larger than the viewport on some axis
	void clampOffset() noexcept;                                                   // per-axis: center if smaller, keep inside if larger
	void setScale(qreal scale) noexcept;                                           // the only writer of _scale and _fitToWindow
	void resetToFit() noexcept;

private:
	QImage _sourceImage;
	QImage _displayImage;
	size_t _cacheKey = 0;

	QString _currentImageFormat;
	qint64 _currentImageFileSize = 0;

	qreal _scale = 1.0;    // device px per source px; 1.0 == 1:1 (native resolution)
	QPointF _offset;       // device-px position of source (0,0) within the viewport
	bool _viewInitialized = false;
	bool _fitToWindow = true; // _scale still fits the whole image, so a resize refits rather than preserving the zoom

	QPointF _panStartOffset;
	QPointF _panStartMouseDevice;
	bool _isPanning = false;
};
