#include "cfilessearchwindow.h"
#include "cfilesystemobject.h"
#include "ccontroller.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <QDebug>
RESTORE_COMPILER_WARNINGS

#define SETTINGS_NAME_TO_FIND     "FileSearchDialog/Ui/NameToFind"
#define SETTINGS_CONTENTS_TO_FIND "FileSearchDialog/Ui/ContentsToFind"
#define SETTINGS_ROOT_FOLDER      "FileSearchDialog/Ui/RootFolder"

CFilesSearchWindow::CFilesSearchWindow(QWidget *parent, const QString& root) :
	QMainWindow(parent),
	ui(new Ui::CFilesSearchWindow),
	_engine(CController::get().fileSearchEngine())
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	ui->searchRoot->addItem(root);

	ui->nameToFind->enableAutoSave(SETTINGS_NAME_TO_FIND);
	ui->fileContentsToFind->enableAutoSave(SETTINGS_CONTENTS_TO_FIND);
	ui->searchRoot->enableAutoSave(SETTINGS_ROOT_FOLDER);

	ui->progressLabel->clear();

	_engine.addListener(this);
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	_engine.removeListener(this);
	_engine.stopSearching();
	delete ui;
}

void CFilesSearchWindow::itemScanned(const QString& currentItem) const
{
	ui->progressLabel->setText(tr("Scanning...") % " " % currentItem);
}

void CFilesSearchWindow::matchFound(const QString& path) const
{
	const bool isDir = CFileSystemObject(path).isDir();
	ui->resultsList->addItem(isDir ? ("[" % path % "]") : path);
}

void CFilesSearchWindow::searchFinished() const
{
	ui->btnSearch->setText(tr("Start"));
	ui->progressLabel->clear();
}

void CFilesSearchWindow::search()
{
	if (_engine.searchInProgress())
	{
		_engine.stopSearching();
		return;
	}

	const QString what = ui->nameToFind->currentText();
	const QString where = ui->searchRoot->currentText();
	const QString withText = ui->fileContentsToFind->currentText();

	_engine.search(what, where, withText);
	ui->btnSearch->setText(tr("Stop"));
	ui->resultsList->clear();
}
