#include "cimageviewerwidget.h"
#include "../../qtutils/imageprocessing/resize/cimageresizer.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QFileInfo>
#include <QImageReader>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CImageViewerWidget::CImageViewerWidget(QWidget *parent) :
	QWidget(parent),
	_imageFileSize(0)
{
	// To avoid double image rendering - on show and on resize
	setUpdatesEnabled(false);
}

bool CImageViewerWidget::displayImage(const QString& imagePath, const QImage& image)
{
	_imageFileSize = QFileInfo(imagePath).size();
	_sourceImage = image.isNull() ? QImageReader(imagePath).read() : image;
	if (!_sourceImage.isNull())
	{
		const QSize screenSize = QApplication::desktop()->availableGeometry().size() - QSize(30, 100);
		QSize widgetSize = _sourceImage.size();
		if (widgetSize.height() > screenSize.height() || widgetSize.width() > screenSize.width())
			widgetSize.scale(screenSize, Qt::KeepAspectRatio);

		resize(widgetSize);

		QTimer::singleShot(0, [this]() {
			// Apparently, we need the timer in order for the resize to actually be applied before parent's resize
			QMainWindow * mainWindow = nullptr;
			for (QWidget * widget = dynamic_cast<QWidget*>(parent()); widget != nullptr; widget = dynamic_cast<QWidget*>(widget->parent()))
			{
				widget->resize(widget->sizeHint());
				if (!mainWindow)
					mainWindow = dynamic_cast<QMainWindow*>(widget);
			}

			if (mainWindow)
			{
				const auto availableGeometry = QApplication::desktop()->availableGeometry();
				mainWindow->move(QPoint(availableGeometry.width()/2 - mainWindow->frameGeometry().width()/2, availableGeometry.height()/2 - mainWindow->frameGeometry().height()/2));
			}

			setUpdatesEnabled(true);
		});

		return true;
	}
	else
	{
		QMessageBox::warning(dynamic_cast<QWidget*>(parent()), tr("Failed to load the image"), tr("Failed to load the image\n\n%1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
		return false;
	}
}

QString CImageViewerWidget::imageInfoString() const
{
	if (_sourceImage.isNull())
		return QString();

	const int numChannels = _sourceImage.isGrayscale() ? 1 : (3 + (_sourceImage.hasAlphaChannel() ? 1 : 0));
	return tr("%1x%2, %3 channels, %4 bits per pixel, compressed to %5 bits per pixel").
			arg(_sourceImage.width()).
			arg(_sourceImage.height()).
			arg(numChannels).
			arg(_sourceImage.bitPlaneCount()).
			arg(QString::number(8 * _imageFileSize / (double(_sourceImage.width()) * _sourceImage.height()), 'f', 2));
}

QSize CImageViewerWidget::sizeHint() const
{
	// This is required for the size magic to work!
	return size();
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
