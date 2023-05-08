#include "cimageviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cimageviewerwindow.h"

#include <QFileDialog>
#include <QLabel>
#include <QShortcut>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CImageViewerWindow::CImageViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent),
	ui(new Ui::CImageViewerWindow)
{
	ui->setupUi(this);
	_imageInfoLabel = new QLabel(this);
	statusBar()->addWidget(_imageInfoLabel);

	connect(ui->actionOpen, &QAction::triggered, this, [this](){
		const QString filtersString = tr("All files (*.*);; GIF (*.gif);; JPEG (*.jpg *.jpeg);; TIFF (*.tif);; PNG (*.png)");
		const QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(), filtersString);
		if (!fileName.isEmpty())
			displayImage(fileName);
	});

	connect(ui->actionReload, &QAction::triggered, this, [this]() {
		displayImage(_currentImagePath);
	});

	connect(ui->actionClose, &QAction::triggered, this, &QMainWindow::close);

	auto* escScut = new QShortcut(QKeySequence(QStringLiteral("Esc")), this, SLOT(close()));
	connect(this, &QAction::destroyed, escScut, &QShortcut::deleteLater);
}

CImageViewerWindow::~CImageViewerWindow() noexcept
{
	delete ui;
}

bool CImageViewerWindow::displayImage(const QString& imagePath)
{
	_currentImagePath = imagePath;
	if (!ui->_imageViewerWidget->displayImage(imagePath))
		return false;

	_imageInfoLabel->setText(ui->_imageViewerWidget->imageInfoString());
	setWindowTitle(imagePath);

	QTimer::singleShot(10, this, [this](){
		setWindowIcon(ui->_imageViewerWidget->imageIcon({ {16, 16}, {32, 32} }));
	});

	return true;
}
