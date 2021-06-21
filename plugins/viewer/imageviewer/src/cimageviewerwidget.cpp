#include "cimageviewerwidget.h"
#include "widgets/widgetutils.h"
#include "imageprocessing/resize/cimageresizer.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QImageReader>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QTimer>
#include <QScreen>
RESTORE_COMPILER_WARNINGS

CImageViewerWidget::CImageViewerWidget(QWidget *parent) :
	QWidget(parent)
{
	// To avoid double image rendering - on show and on resize
	setUpdatesEnabled(false);
}

bool CImageViewerWidget::displayImage(const QImage& image)
{
	_sourceImage = image;
	if (image.isNull())
		return false;

	const auto * const currentScreen = QApplication::screenAt(geometry().center());
	const auto availableGeometry = currentScreen ? currentScreen->availableGeometry() : QApplication::primaryScreen()->availableGeometry();
	const QSize screenSize = availableGeometry.size() - QSize(30, 100);

	QSize widgetSize = _sourceImage.size();
	if (widgetSize.height() > screenSize.height() || widgetSize.width() > screenSize.width())
		widgetSize.scale(screenSize, Qt::KeepAspectRatio);

	resize(widgetSize);

	QTimer::singleShot(0, this, [this, availableGeometry]() {
		// Apparently, we need the timer in order for the resize to actually be applied before parent's resize
		auto mainWindow = WidgetUtils::findParentMainWindow(this);
		if (mainWindow)
		{
			mainWindow->move(QPoint(availableGeometry.width()/2 - mainWindow->frameGeometry().width()/2, availableGeometry.height()/2 - mainWindow->frameGeometry().height()/2));
		}

		setUpdatesEnabled(true);
	});

	return true;
}

bool CImageViewerWidget::displayImage(const QString& imagePath)
{
	QImageReader reader(imagePath);
	reader.setDecideFormatFromContent(true);

	_currentImageFormat = QString::fromLatin1(reader.format()); // Must be called before read()
	QImage img = reader.read();
	if (!img.isNull())
	{
		_currentImageFileSize = reader.device()->size();
		return displayImage(img);
	}

	_currentImageFileSize = 0;
	_currentImageFormat.clear();

	QMessageBox::warning(dynamic_cast<QWidget*>(parent()), tr("Failed to load the image"), tr("Failed to load the image\n\n%1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
	return false;
}

QString CImageViewerWidget::imageInfoString() const
{
	if (_sourceImage.isNull())
		return QString();

	const int numChannels = _sourceImage.isGrayscale() ? 1 : (3 + (_sourceImage.hasAlphaChannel() ? 1 : 0));
	return _currentImageFormat.toUpper() + ' ' + tr("%1x%2, %3 channels, %4 bits per pixel, compressed to %5 bits per pixel").
		arg(_sourceImage.width()).
		arg(_sourceImage.height()).
		arg(numChannels).
		arg(_sourceImage.bitPlaneCount()).
		arg(QString::number(8 * _currentImageFileSize / (double(_sourceImage.width()) * _sourceImage.height()), 'f', 2));
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
	{
		for (QSize s : sizes)
			result.addPixmap(QPixmap::fromImage(CImageResizer::resize(_sourceImage, s, CImageResizer::Smart)));
	}

	return result;
}

void CImageViewerWidget::paintEvent(QPaintEvent*)
{
	if (_scaledImage.isNull() || _scaledImage.size() != _sourceImage.size().scaled(size(), Qt::KeepAspectRatio))
		_scaledImage = CImageResizer::resize(_sourceImage, size(), CImageResizer::Smart);

	if (!_sourceImage.isNull())
		QPainter(this).drawImage(0, 0, _scaledImage);
}
