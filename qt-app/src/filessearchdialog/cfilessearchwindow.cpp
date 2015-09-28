#include "cfilessearchwindow.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"
RESTORE_COMPILER_WARNINGS

#define SETTINGS_NAME_TO_FIND     "FileSearchDialog/Ui/NameToFind"
#define SETTINGS_CONTENTS_TO_FIND "FileSearchDialog/Ui/ContentsToFind"
#define SETTINGS_ROOT_FOLDER      "FileSearchDialog/Ui/RootFolder"

CFilesSearchWindow::CFilesSearchWindow(QWidget *parent, const QString& root) :
	QMainWindow(parent),
	ui(new Ui::CFilesSearchWindow)
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	ui->searchRoot->addItem(root);

	ui->nameToFind->enableAutoSave(SETTINGS_NAME_TO_FIND);
	ui->fileContentsToFind->enableAutoSave(SETTINGS_CONTENTS_TO_FIND);
	ui->searchRoot->enableAutoSave(SETTINGS_ROOT_FOLDER);
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	delete ui;
}

void CFilesSearchWindow::search()
{
	const QString what = ui->nameToFind->currentText();
	const QString withText = ui->fileContentsToFind->currentText();


}