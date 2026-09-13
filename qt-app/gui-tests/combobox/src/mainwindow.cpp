#include "mainwindow.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "widgets/chistorycombobox.h"


DISABLE_COMPILER_WARNINGS
#include "ui_mainwindow.h"
RESTORE_COMPILER_WARNINGS

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);
}

MainWindow::~MainWindow()
{
	delete ui;
}
