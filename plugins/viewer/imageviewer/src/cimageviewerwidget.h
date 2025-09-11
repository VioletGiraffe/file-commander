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
public:
	explicit CImageViewerWidget(QWidget *parent = nullptr) noexcept;

public:
	bool displayImage(const QImage& image);
	bool displayImage(const QString& imagePath);
	[[nodiscard]] QString imageInfoString() const;

	[[nodiscard]] QSize sizeHint() const override;

	[[nodiscard]] QIcon imageIcon(const std::vector<QSize>& sizes) const;

	void copyToClipboard() noexcept;

protected:
	void paintEvent(QPaintEvent* e) override;

private:
	QImage _sourceImage;
	QImage _scaledImage;

	QString _currentImageFormat;
	qint64 _currentImageFileSize = 0;
};
