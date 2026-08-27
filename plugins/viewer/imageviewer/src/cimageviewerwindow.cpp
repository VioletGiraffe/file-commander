#include "cimageviewerwindow.h"
#include "plugininterface/cpluginproxy.h"
#include "widgets/cimageviewerwidget.h"

DISABLE_COMPILER_WARNINGS
#include "resize/qimage_resize.h"
#include "ui_cimageviewerwindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <utility>

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
			CImageViewerWidget::smoothScale(dest, source, srcRect);
	});
	_imageInfoLabel = new QLabel(this);
	statusBar()->addWidget(_imageInfoLabel);
	_viewingAtLabel = new QLabel(this);
	statusBar()->addWidget(_viewingAtLabel);

	connect(ui->actionOpen, &QAction::triggered, this, [this] {
		const QString filtersString = tr("All files (*.*);; GIF (*.gif);; JPEG (*.jpg *.jpeg *.jpe);; TIFF (*.tif *.tiff);; PNG (*.png)");
		const QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(), filtersString);
		if (!fileName.isEmpty())
			displayImage(fileName);
	});

	connect(ui->actionReload, &QAction::triggered, this, [this] {
		displayImage(_currentImagePath);
	});

	connect(ui->actionSaveAs, &QAction::triggered, this, &CImageViewerWindow::saveImageAs);

	connect(ui->actionClose, &QAction::triggered, this, &QMainWindow::close);

	connect(ui->action_Copy_to_clipboard, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::copyToClipboard);
	connect(ui->action_Copy_to_clipboard_as_displayed, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::copyDisplayedToClipboard);

	connect(ui->actionFitToScreen, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::fitToWindow);
	connect(ui->actionZoom1to1, &QAction::triggered, ui->_imageViewerWidget, &CImageViewerWidget::zoomToActualPixels);

	connect(ui->_imageViewerWidget, &CImageViewerWidget::displayedSizeChanged, this, [this](QSize size, qreal magnification) {
		_viewingAtLabel->setText(tr("Viewing at %1x%2 (%3% / %4x)").arg(size.width()).arg(size.height()).arg(qRound(magnification * 100.0)).arg(magnification, 0, 'f', 2));
	});

	new QShortcut(QKeySequence(QStringLiteral("Esc")), this, SLOT(close()));
}

CImageViewerWindow::~CImageViewerWindow() noexcept
{
	delete ui;
}

bool CImageViewerWindow::displayImage(const QString& imagePath)
{
	_currentImagePath = imagePath;
	if (!ui->_imageViewerWidget->displayImage(imagePath))
	{
		QMessageBox::warning(this, tr("Failed to load the image"), tr("Failed to load the image %1\n\nIt is inaccessible, doesn't exist or is not a supported image file.").arg(imagePath));
		return false;
	}

	_imageInfoLabel->setText(ui->_imageViewerWidget->imageInfoString());
	QFileInfo fi(imagePath);
	setWindowTitle(fi.fileName() + " [" + fi.absolutePath() + "]");
	// Only has effect on macOS , must be after set. Gives the title bar a document proxy icon (the only icon this window can have)
	setWindowFilePath(imagePath);

	QTimer::singleShot(3000, this, [this](){
		setWindowIcon(ui->_imageViewerWidget->imageIcon());
	});

	return true;
}

void CImageViewerWindow::saveImageAs()
{
	const QImage& image = ui->_imageViewerWidget->sourceImage();
	if (image.isNull())
		return;

	const QString pngFilter = tr("PNG image (*.png)");
	const QString jpegFilter = tr("JPEG image (*.jpg *.jpeg)");
	const QString tiffFilter = tr("TIFF image (*.tif *.tiff)");

	// Suggest the source's folder and base name but no extension, so the chosen filter picks the output
	// format instead of a stale extension carried over from the source silently overriding it.
	const QFileInfo sourceInfo(_currentImagePath);
	const QString suggestedPath = sourceInfo.absolutePath() + '/' + sourceInfo.completeBaseName();

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
		writer.setQuality(85);

	if (!writer.write(image))
	{
		QMessageBox::warning(this, tr("Failed to save the image"),
			tr("Failed to save the image to \"%1\":\n\n%2").arg(fileName, writer.errorString()));
	}
}
