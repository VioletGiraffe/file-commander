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
	setWindowIcon(ui->_imageViewerWidget->imageIcon(QSize(64, 64)));
}
