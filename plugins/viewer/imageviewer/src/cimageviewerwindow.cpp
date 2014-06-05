#include "cimageviewerwindow.h"
#include "ui_cimageviewerwindow.h"

#include <QLabel>

CImageViewerWindow::CImageViewerWindow(QWidget *parent) :
	CPluginWindow(parent),
	ui(new Ui::CImageViewerWindow)
{
	ui->setupUi(this);
	_imageInfoLabel = new QLabel(this);
	statusBar()->addWidget(_imageInfoLabel);
}

CImageViewerWindow::~CImageViewerWindow()
{
	delete ui;
}

void CImageViewerWindow::displayImage(const QString & imagePath)
{
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
