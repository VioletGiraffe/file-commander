#include "cimageviewerwindow.h"
#include "ui_cimageviewerwindow.h"

#include <QLabel>
#include <QFileDialog>

CImageViewerWindow::CImageViewerWindow(QWidget *parent) :
	CPluginWindow(parent),
	ui(new Ui::CImageViewerWindow)
{
	ui->setupUi(this);
	_imageInfoLabel = new QLabel(this);
	statusBar()->addWidget(_imageInfoLabel);

	connect(ui->actionOpen, &QAction::triggered, [this](){
		const QString filtersString = "All files (*.*);; GIF (*.gif);; JPEG (*.jpg *.jpeg);; TIFF (*.tif);; PNG (*.png)";
		const QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(), filtersString);
		if (!fileName.isEmpty())
			displayImage(fileName);
	});

	connect(ui->actionReload, &QAction::triggered, [this](){
		displayImage(_currentImagePath);
	});

	connect(ui->actionClose, SIGNAL(triggered()), SLOT(close()));
}

CImageViewerWindow::~CImageViewerWindow()
{
	delete ui;
}

void CImageViewerWindow::displayImage(const QString & imagePath)
{
	_currentImagePath = imagePath;
	ui->_imageViewerWidget->displayImage(imagePath);
	_imageInfoLabel->setText(ui->_imageViewerWidget->imageInfoString());
	adjustSize();
	setWindowTitle(imagePath);
	static QList<QSize> requiredIconSizes;
	if (requiredIconSizes.empty())
	{
#ifdef _WIN32
		requiredIconSizes << QSize(16, 16) << QSize(32, 32);
#elif defined __APPLE__
		requiredIconSizes << QSize(16, 16) << QSize(32, 32);
#else
		requiredIconSizes << QSize(16, 16) << QSize(32, 32);
#endif
	}

	setWindowIcon(ui->_imageViewerWidget->imageIcon(requiredIconSizes));
}
