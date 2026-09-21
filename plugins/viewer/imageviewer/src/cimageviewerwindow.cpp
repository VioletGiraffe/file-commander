#include "cimageviewerwindow.h"
#include "cimageinfodialog.h"

#include "plugininterface/cpluginproxy.h"


// Submodule includes
#include "qtcore_helpers/qstring_helpers.hpp"
#include "resize/qimage_resize.h"
#include "widgets/cimageviewerwidget.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cimageviewerwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QMenu>
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
	ui(new Ui::CImageViewerWindow)
{
	ui->setupUi(this);
	// The proxy outlives every plugin window, so the captured pointer stays valid in copies of the scaler that outlive this window.
	ImageProcessing::ParallelForFn parallelFor = [proxy = &proxy](size_t count, const std::function<void(size_t)>& body) {
		proxy->parallelFor(count, body);
	};
	ui->_imageViewerWidget->setImageScaler([parallelFor = std::move(parallelFor)](QImage& dest, const QImage& source, const QRect& srcRect) {
		if (!ImageProcessing::resize(dest, source, srcRect, parallelFor))
			CImageViewerWidget::smoothScaleQt(dest, source, srcRect);
	});

	ui->_imageViewerWidget->setInfoStripHint(tr("Press %1 to hide").arg(ui->actionShowInfoStrip->shortcut().toString(QKeySequence::NativeText)));
	connect(ui->actionShowInfoStrip, &QAction::toggled, ui->_imageViewerWidget, &CImageViewerWidget::setOverlayVisible);

	// Built here rather than in the .ui: Qt Designer offers no way to create an action group.
	auto* upscalingModeGroup = new QActionGroup(this);
	upscalingModeGroup->addAction(ui->actionSmoothUpscaling);
	upscalingModeGroup->addAction(ui->actionPixelPreservingUpscaling);

	const bool pixelPreservingUpscaling = QSettings{}.value(SETTINGS_PIXEL_PRESERVING_UPSCALING, false).toBool();
	(pixelPreservingUpscaling ? ui->actionPixelPreservingUpscaling : ui->actionSmoothUpscaling)->setChecked(true);
	ui->_imageViewerWidget->setNearestNeighborUpscaling(pixelPreservingUpscaling);
	// Only this action is connected: the group unchecks it whenever the smooth one is picked.
	connect(ui->actionPixelPreservingUpscaling, &QAction::toggled, this, [this](bool enabled) {
		ui->_imageViewerWidget->setNearestNeighborUpscaling(enabled);
		if (!_quickViewMode) // Quick view forces the mode, so it must not overwrite the stored preference.
			QSettings{}.setValue(SETTINGS_PIXEL_PRESERVING_UPSCALING, enabled);
	});

	// Checking the other action is what switches modes: unchecking the current one would leave the group with no selection.
	const QKeySequence upscalingModeToggleKey{ QStringLiteral("P") };
	new QShortcut(upscalingModeToggleKey, this, this, [this] {
		(ui->actionPixelPreservingUpscaling->isChecked() ? ui->actionSmoothUpscaling : ui->actionPixelPreservingUpscaling)->setChecked(true);
	});

	// A menu draws the text after a tab in its shortcut column. The key switches between the two actions,
	// so neither can own it as its own shortcut, and both advertise it this way instead.
	for (QAction* action : { ui->actionSmoothUpscaling, ui->actionPixelPreservingUpscaling })
		action->setText(action->text() + '\t' + upscalingModeToggleKey.toString(QKeySequence::NativeText));

	connect(ui->actionOpen, &QAction::triggered, this, [this] {
		const QString filtersString = tr("All files (*.*);; GIF (*.gif);; JPEG (*.jpg *.jpeg *.jpe);; TIFF (*.tif *.tiff);; PNG (*.png)");
		const QString fileName = QFileDialog::getOpenFileName(dialogParent(), QString(), QString(), filtersString);
		if (!fileName.isEmpty())
			displayImage(fileName);
	});

	connect(ui->actionReload, &QAction::triggered, this, [this] {
		displayImage(_currentImagePath, false); // Same file: the current zoom and pan still apply
	});

	connect(ui->actionSaveAs, &QAction::triggered, this, &CImageViewerWindow::saveImageAs);

	connect(ui->actionImageInfo, &QAction::triggered, this, &CImageViewerWindow::showImageInfo);

	connect(ui->actionClose, &QAction::triggered, this, &QMainWindow::close);

	connect(ui->action_Copy_to_clipboard, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::copyToClipboard);
	connect(ui->action_Copy_to_clipboard_as_displayed, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::copyDisplayedToClipboard);

	connect(ui->actionFitToScreen, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::fitToWindow);
	connect(ui->actionZoom1to1, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::zoomToActualPixels);
	connect(ui->actionPauseAnimation, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::togglePause);
	connect(ui->actionPreviousFrame, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::stepToPreviousFrame);
	connect(ui->actionNextFrame, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::stepToNextFrame);
	// Resolved here rather than on load: an animation also ends on its own, and this is the only place the items are seen.
	connect(ui->menuView, &QMenu::aboutToShow, this, [this] {
		const bool animated = ui->_imageViewerWidget->isAnimated();
		for (QAction* action : { ui->actionPauseAnimation, ui->actionPreviousFrame, ui->actionNextFrame })
			action->setEnabled(animated);
	});

	new QShortcut(QKeySequence(QStringLiteral("Esc")), this, SLOT(close()));
}

CImageViewerWindow::~CImageViewerWindow() noexcept
{
	delete ui;
}

bool CImageViewerWindow::displayImage(const QString& imagePath, bool resetViewParameters)
{
	_currentImagePath = imagePath;
	if (!ui->_imageViewerWidget->displayImage(imagePath, resetViewParameters))
	{
		QMessageBox::warning(dialogParent(), tr("Failed to load the image"), tr("Failed to load the image %1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
		return false;
	}

	QFileInfo fi(imagePath);
	setWindowTitle(fi.fileName() + " [" + fi.absolutePath() + "]");
	// Only has effect on macOS , must be after set. Gives the title bar a document proxy icon (the only icon this window can have)
	setWindowFilePath(imagePath);

	QTimer::singleShot(3000, this, [this](){
		setWindowIcon(ui->_imageViewerWidget->imageIcon());
	});

	return true;
}

void CImageViewerWindow::configureForQuickView()
{
	_quickViewMode = true; // Must precede the check: the toggle handler reads it to skip the settings write.
	ui->actionPixelPreservingUpscaling->setChecked(true);

	// The menu bar stays with this window, which is never shown, so the canvas offers the applicable actions instead.
	// The upscaling mode is not among them: quick view forces it, and every window here is built anew for each file.
	QAction* const contextMenuActions[] = {
		ui->actionFitToScreen, ui->actionZoom1to1,
		nullptr, // Separator
		ui->actionShowInfoStrip, ui->actionImageInfo,
		nullptr,
		ui->action_Copy_to_clipboard, ui->action_Copy_to_clipboard_as_displayed, ui->actionSaveAs
	};

	for (QAction* action : contextMenuActions)
	{
		if (!action)
		{
			action = new QAction(this);
			action->setSeparator(true);
		}
		// Adding an action to a visible widget puts its shortcut up against the main window's own. Ctrl+0 is the only one free there.
		else if (action != ui->actionFitToScreen)
			action->setShortcut({});

		ui->_imageViewerWidget->addAction(action);
	}

	ui->_imageViewerWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
	ui->_imageViewerWidget->setInfoStripHint(tr("Right-click for options"));
}

QWidget* CImageViewerWindow::dialogParent() const
{
	return ui->_imageViewerWidget->window();
}

void CImageViewerWindow::showImageInfo()
{
	CImageInfoDialog dialog{ _currentImagePath, ui->_imageViewerWidget->sourceImage(), dialogParent() };
	dialog.exec();
}

void CImageViewerWindow::saveImageAs()
{
	// Copied: the dialogs below run an event loop, and this must stay the image the user chose to save.
	const QImage image = ui->_imageViewerWidget->sourceImage();
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
