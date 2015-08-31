#include "cfilessearchwindow.h"
#include "ui_cfilessearchwindow.h"

CFilesSearchWindow::CFilesSearchWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::CFilesSearchWindow)
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	delete ui;
}
