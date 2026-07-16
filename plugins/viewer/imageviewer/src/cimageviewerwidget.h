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
	[[nodiscard]] QString imageInfoString() const;

	[[nodiscard]] QSize sizeHint() const override;

	[[nodiscard]] QIcon imageIcon(const std::vector<QSize>& sizes) const;

	void copyToClipboard() noexcept;
	void copyDisplayedToClipboard() noexcept;

signals:
	// magnification is the ratio of on-screen pixels to source-image pixels, i.e. 1.0 = native resolution ("100%")
	void displayedSizeChanged(QSize size, qreal magnification);

protected:
	void paintEvent(QPaintEvent* e) override;
	void wheelEvent(QWheelEvent* e) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;

private:
	QImage _sourceImage;
	QImage _displayImage;
	size_t _cacheKey = 0;

	QString _currentImageFormat;
	qint64 _currentImageFileSize = 0;

	qreal _zoom = 1.0;
	QPointF _imageCenterUv;
	QPointF _panStartCenterUv;
	QPoint _panStartMousePos;
	bool _isPanning = false;
};
