#include "cimageviewer.h"
#include "cimageinfodialog.h"

#include "plugininterface/cpluginproxy.h"


// Submodule includes
#include "resize/qimage_resize.h"
#include "widgets/cimageviewerwidget.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <utility>

CImageViewer::CImageViewer(CPluginProxy& proxy) noexcept :
	_imageViewerWidget(new CImageViewerWidget(this))
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(_imageViewerWidget);

	// The proxy outlives every viewer, so the captured pointer stays valid in copies of the scaler that outlive this one.
	ImageProcessing::ParallelForFn parallelFor = [proxy = &proxy](size_t count, const std::function<void(size_t)>& body) {
		proxy->parallelFor(count, body);
	};
	_imageViewerWidget->setImageScaler([parallelFor = std::move(parallelFor)](QImage& dest, const QImage& source, const QRect& srcRect, Qt::TransformationMode mode) {
		const auto kernel = mode == Qt::FastTransformation ? ImageProcessing::ResizeKernel::NearestNeighbor : ImageProcessing::ResizeKernel::Auto;
		if (!ImageProcessing::resize(dest, source, srcRect, parallelFor, kernel))
			CImageViewerWidget::scaleWithQt(dest, source, srcRect, mode);
	});

	auto* editMenu = new QMenu(tr("&Edit"), this);
	editMenu->addAction(tr("&Copy to clipboard"), QKeySequence{ Qt::CTRL | Qt::Key_C }, _imageViewerWidget, &CImageViewerWidget::copyToClipboard);
	editMenu->addAction(tr("Copy to clipboard as displa&yed"), _imageViewerWidget, &CImageViewerWidget::copyDisplayedToClipboard);
	addMenu(editMenu);

	auto* viewMenu = new QMenu(tr("&View"), this);
	viewMenu->addAction(tr("&Fit to screen"), QKeySequence{ Qt::CTRL | Qt::Key_0 }, _imageViewerWidget, &CImageViewerWidget::fitToWindow);
	viewMenu->addAction(tr("&Zoom 1:1"), QKeySequence{ Qt::CTRL | Qt::Key_1 }, _imageViewerWidget, &CImageViewerWidget::zoomToActualPixels);
	viewMenu->addSeparator();
	QAction* pauseAnimationAction = viewMenu->addAction(tr("Pause/resume &animation"), QKeySequence{ Qt::Key_Space }, _imageViewerWidget, &CImageViewerWidget::togglePause);
	QAction* previousFrameAction = viewMenu->addAction(tr("Pre&vious frame"), QKeySequence{ Qt::Key_Left }, _imageViewerWidget, &CImageViewerWidget::stepToPreviousFrame);
	QAction* nextFrameAction = viewMenu->addAction(tr("&Next frame"), QKeySequence{ Qt::Key_Right }, _imageViewerWidget, &CImageViewerWidget::stepToNextFrame);
	// Resolved here rather than on load: an animation also ends on its own, and this is the only place the items are seen.
	connect(viewMenu, &QMenu::aboutToShow, this, [this, pauseAnimationAction, previousFrameAction, nextFrameAction] {
		const bool animated = _imageViewerWidget->isAnimated();
		for (QAction* action : { pauseAnimationAction, previousFrameAction, nextFrameAction })
			action->setEnabled(animated);
	});
	viewMenu->addSeparator();

	QAction* showInfoStripAction = viewMenu->addAction(tr("Show the &info strip"), QKeySequence{ Qt::Key_I });
	showInfoStripAction->setCheckable(true);
	showInfoStripAction->setChecked(true);
	_imageViewerWidget->setInfoStripHint(tr("Press %1 to hide, right-click for options").arg(showInfoStripAction->shortcut().toString(QKeySequence::NativeText)));
	connect(showInfoStripAction, &QAction::toggled, _imageViewerWidget, &CImageViewerWidget::setOverlayVisible);
	addMenu(viewMenu);

	auto* imageMenu = new QMenu(tr("&Image"), this);
	imageMenu->addAction(tr("Image &info..."), QKeySequence{ Qt::ALT | Qt::Key_Return }, this, &CImageViewer::showImageInfo);
	imageMenu->addAction(tr("Save &As..."), QKeySequence{ Qt::CTRL | Qt::SHIFT | Qt::Key_S }, this, &CImageViewer::saveImageAs);
	addMenu(imageMenu);
}

QString CImageViewer::tryLoadFile(const QString& path)
{
	// The view parameters carry over between files: a zoomed view keeps its zoom, a fitted one refits
	if (!_imageViewerWidget->displayImage(path, false))
		return tr("Failed to load the image %1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(path);

	_currentImagePath = path;
	QTimer::singleShot(3000, this, [this] {
		setWindowIcon(_imageViewerWidget->imageIcon()); // A hosting window follows it
	});

	return {};
}

void CImageViewer::closeFile()
{
	_imageViewerWidget->closeFile();
}

void CImageViewer::showImageInfo()
{
	CImageInfoDialog dialog{ _currentImagePath, _imageViewerWidget->sourceImage(), this };
	dialog.exec();
}

void CImageViewer::saveImageAs()
{
	// Copied: the dialogs below run an event loop, and this must stay the image the user chose to save.
	const QImage image = _imageViewerWidget->sourceImage();
	if (image.isNull())
		return;

	const QString pngFilter = tr("PNG image (*.png)");
	const QString jpegFilter = tr("JPEG image (*.jpg *.jpeg)");
	const QString tiffFilter = tr("TIFF image (*.tif *.tiff)");

	// The suggested name must carry an extension: the native Windows dialog leaves the name box empty without one.
	// It matches the initially selected filter below, and the dialog rewrites it whenever the filter changes.
	const QFileInfo sourceInfo(_currentImagePath);
	const QString suggestedPath = sourceInfo.absolutePath() + '/' + sourceInfo.completeBaseName() + ".png";

	QString selectedFilter = pngFilter;
	QString fileName = QFileDialog::getSaveFileName(this, tr("Save image as"), suggestedPath,
		pngFilter + ";;" + jpegFilter + ";;" + tiffFilter, &selectedFilter);
	if (fileName.isEmpty())
		return;

	QString suffix = QFileInfo(fileName).suffix().toLower();
	if (suffix.isEmpty()) // The user typed no extension; take it from the chosen filter and append it.
	{
		suffix = selectedFilter == jpegFilter ? "jpg" : (selectedFilter == tiffFilter ? "tif" : "png");
		fileName += '.' + suffix;
	}

	const bool isJpeg = suffix == "jpg" || suffix == "jpeg" || suffix == "jpe";
	if (isJpeg && image.hasAlphaChannel())
	{
		const auto answer = QMessageBox::warning(this, tr("Transparency will be lost"),
			tr("This image has transparency, which the JPEG format cannot store. The transparency will be discarded, so formerly transparent areas will become opaque and may show unexpected colors.\n\nSave as JPEG anyway?"),
			QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer != QMessageBox::Save)
			return;
	}

	QImageWriter writer(fileName);
	if (suffix == "png")
		writer.setCompression(100); // Qt maps this to the maximum zlib level for PNG (lossless).
	else if (suffix == "tif" || suffix == "tiff")
		writer.setCompression(1); // Any non-zero value selects LZW for TIFF (lossless); the default would be uncompressed.
	else if (isJpeg)
	{
		if (image.size().width() * image.size().height() < 500 * 500)
			writer.setQuality(96);
		else
			writer.setQuality(85);
	}

	if (!writer.write(image))
	{
		QMessageBox::warning(this, tr("Failed to save the image"),
			tr("Failed to save the image to \"%1\":\n\n%2").arg(fileName, writer.errorString()));
	}
}
