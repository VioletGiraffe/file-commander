#include "cimageviewerwindow.h"
#include "cimageinfodialog.h"

#include "plugininterface/cpluginproxy.h"


// Submodule includes
#include "qtcore_helpers/qstring_helpers.hpp"
#include "resize/qimage_resize.h"
#include "widgets/cimageviewerwidget.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <utility>

#define SETTINGS_PIXEL_PRESERVING_UPSCALING QSL("Plugins/ImageViewer/PixelPreservingUpscaling")

CImageViewerWindow::CImageViewerWindow(CPluginProxy& proxy, QWidget* parent) noexcept :
	CPluginWindow(parent),
	_imageViewerWidget(new CImageViewerWidget(this))
{
	setCentralWidget(_imageViewerWidget);

	// The proxy outlives every plugin window, so the captured pointer stays valid in copies of the scaler that outlive this window.
	ImageProcessing::ParallelForFn parallelFor = [proxy = &proxy](size_t count, const std::function<void(size_t)>& body) {
		proxy->parallelFor(count, body);
	};
	_imageViewerWidget->setImageScaler([parallelFor = std::move(parallelFor)](QImage& dest, const QImage& source, const QRect& srcRect) {
		if (!ImageProcessing::resize(dest, source, srcRect, parallelFor))
			CImageViewerWidget::smoothScaleQt(dest, source, srcRect);
	});

	QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
	fileMenu->addAction(tr("&Open..."), QKeySequence{ Qt::CTRL | Qt::Key_O }, this, [this] {
		const QString filtersString = tr("All files (*.*);; GIF (*.gif);; JPEG (*.jpg *.jpeg *.jpe);; TIFF (*.tif *.tiff);; PNG (*.png)");
		const QString fileName = QFileDialog::getOpenFileName(dialogParent(), QString(), QString(), filtersString);
		if (!fileName.isEmpty())
			displayImage(fileName);
	});
	fileMenu->addAction(tr("&Reload"), QKeySequence{ Qt::Key_F5 }, this, [this] {
		displayImage(_currentImagePath, false); // Same file: the current zoom and pan still apply
	});
	fileMenu->addSeparator();
	_imageInfoAction = fileMenu->addAction(tr("Image &info..."), QKeySequence{ Qt::ALT | Qt::Key_Return }, this, &CImageViewerWindow::showImageInfo);
	fileMenu->addSeparator();
	_saveAsAction = fileMenu->addAction(tr("Save &As..."), QKeySequence{ Qt::CTRL | Qt::SHIFT | Qt::Key_S }, this, &CImageViewerWindow::saveImageAs);
	fileMenu->addSeparator();
	fileMenu->addAction(tr("&Close"), this, &QWidget::close);

	QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
	_copyAction = editMenu->addAction(tr("&Copy to clipboard"), QKeySequence{ Qt::CTRL | Qt::Key_C }, _imageViewerWidget, &CImageViewerWidget::copyToClipboard);
	_copyAsDisplayedAction = editMenu->addAction(tr("Copy to clipboard as displa&yed"), _imageViewerWidget, &CImageViewerWidget::copyDisplayedToClipboard);

	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
	_fitToScreenAction = viewMenu->addAction(tr("&Fit to screen"), QKeySequence{ Qt::CTRL | Qt::Key_0 }, _imageViewerWidget, &CImageViewerWidget::fitToWindow);
	_zoom1to1Action = viewMenu->addAction(tr("&Zoom 1:1"), QKeySequence{ Qt::CTRL | Qt::Key_1 }, _imageViewerWidget, &CImageViewerWidget::zoomToActualPixels);
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

	_showInfoStripAction = viewMenu->addAction(tr("Show the &info strip"), QKeySequence{ Qt::Key_I });
	_showInfoStripAction->setCheckable(true);
	_showInfoStripAction->setChecked(true);
	_imageViewerWidget->setInfoStripHint(tr("Press %1 to hide").arg(_showInfoStripAction->shortcut().toString(QKeySequence::NativeText)));
	connect(_showInfoStripAction, &QAction::toggled, _imageViewerWidget, &CImageViewerWidget::setOverlayVisible);
	viewMenu->addSeparator();

	QAction* smoothUpscalingAction = viewMenu->addAction(tr("&Smooth upscaling"));
	_pixelPreservingUpscalingAction = viewMenu->addAction(tr("&Pixel-preserving upscaling"));
	auto* upscalingModeGroup = new QActionGroup(this);
	for (QAction* action : { smoothUpscalingAction, _pixelPreservingUpscalingAction })
	{
		action->setCheckable(true);
		upscalingModeGroup->addAction(action);
	}

	const bool pixelPreservingUpscaling = QSettings{}.value(SETTINGS_PIXEL_PRESERVING_UPSCALING, false).toBool();
	(pixelPreservingUpscaling ? _pixelPreservingUpscalingAction : smoothUpscalingAction)->setChecked(true);
	_imageViewerWidget->setNearestNeighborUpscaling(pixelPreservingUpscaling);
	// Only this action is connected: the group unchecks it whenever the smooth one is picked.
	connect(_pixelPreservingUpscalingAction, &QAction::toggled, this, [this](bool enabled) {
		_imageViewerWidget->setNearestNeighborUpscaling(enabled);
		if (!_quickViewMode) // Quick view forces the mode, so it must not overwrite the stored preference.
			QSettings{}.setValue(SETTINGS_PIXEL_PRESERVING_UPSCALING, enabled);
	});

	// Checking the other action is what switches modes: unchecking the current one would leave the group with no selection.
	const QKeySequence upscalingModeToggleKey{ QStringLiteral("P") };
	new QShortcut(upscalingModeToggleKey, this, this, [this, smoothUpscalingAction] {
		(_pixelPreservingUpscalingAction->isChecked() ? smoothUpscalingAction : _pixelPreservingUpscalingAction)->setChecked(true);
	});

	// A menu draws the text after a tab in its shortcut column. The key switches between the two actions,
	// so neither can own it as its own shortcut, and both advertise it this way instead.
	for (QAction* action : { smoothUpscalingAction, _pixelPreservingUpscalingAction })
		action->setText(action->text() + '\t' + upscalingModeToggleKey.toString(QKeySequence::NativeText));

	new QShortcut(QKeySequence(QStringLiteral("Esc")), this, SLOT(close()));
}

bool CImageViewerWindow::displayImage(const QString& imagePath, bool resetViewParameters)
{
	_currentImagePath = imagePath;
	if (!_imageViewerWidget->displayImage(imagePath, resetViewParameters))
	{
		QMessageBox::warning(dialogParent(), tr("Failed to load the image"), tr("Failed to load the image %1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
		return false;
	}

	QFileInfo fi(imagePath);
	setWindowTitle(fi.fileName() + " [" + fi.absolutePath() + "]");
	// Only has effect on macOS , must be after set. Gives the title bar a document proxy icon (the only icon this window can have)
	setWindowFilePath(imagePath);

	QTimer::singleShot(3000, this, [this](){
		setWindowIcon(_imageViewerWidget->imageIcon());
	});

	return true;
}

void CImageViewerWindow::configureForQuickView()
{
	_quickViewMode = true; // Must precede the check: the toggle handler reads it to skip the settings write.
	_pixelPreservingUpscalingAction->setChecked(true);

	// The menu bar stays with this window, which is never shown, so the canvas offers the applicable actions instead.
	// The upscaling mode is not among them: quick view forces it, and every window here is built anew for each file.
	QAction* const contextMenuActions[] = {
		_fitToScreenAction, _zoom1to1Action,
		nullptr, // Separator
		_showInfoStripAction, _imageInfoAction,
		nullptr,
		_copyAction, _copyAsDisplayedAction, _saveAsAction
	};

	for (QAction* action : contextMenuActions)
	{
		if (!action)
		{
			action = new QAction(this);
			action->setSeparator(true);
		}
		// Adding an action to a visible widget puts its shortcut up against the main window's own. Ctrl+0 is the only one free there.
		else if (action != _fitToScreenAction)
			action->setShortcut({});

		_imageViewerWidget->addAction(action);
	}

	_imageViewerWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
	_imageViewerWidget->setInfoStripHint(tr("Right-click for options"));
}

QWidget* CImageViewerWindow::dialogParent() const
{
	return _imageViewerWidget->window();
}

void CImageViewerWindow::showImageInfo()
{
	CImageInfoDialog dialog{ _currentImagePath, _imageViewerWidget->sourceImage(), dialogParent() };
	dialog.exec();
}

void CImageViewerWindow::saveImageAs()
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
	QString fileName = QFileDialog::getSaveFileName(dialogParent(), tr("Save image as"), suggestedPath,
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
		const auto answer = QMessageBox::warning(dialogParent(), tr("Transparency will be lost"),
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
		QMessageBox::warning(dialogParent(), tr("Failed to save the image"),
			tr("Failed to save the image to \"%1\":\n\n%2").arg(fileName, writer.errorString()));
	}
}
