#include "cfilessearchwindow.h"
#include "cfilesystemobject.h"

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
	_workerThread("File search thread")
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	ui->searchRoot->addItem(root);

	ui->nameToFind->enableAutoSave(SETTINGS_NAME_TO_FIND);
	ui->fileContentsToFind->enableAutoSave(SETTINGS_CONTENTS_TO_FIND);
	ui->searchRoot->enableAutoSave(SETTINGS_ROOT_FOLDER);

	ui->progressLabel->clear();

	connect(&_uiUpdateTimer, &QTimer::timeout, this, &CFilesSearchWindow::updateUi);
	_uiUpdateTimer.setInterval(100);
	_uiUpdateTimer.start();
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	_workerThread.interrupt();

	_uiUpdateTimer.stop();
	disconnect(&_uiUpdateTimer);
	delete ui;
}

void CFilesSearchWindow::search()
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return;
	}

	const QString what = ui->nameToFind->currentText();
	const QString where = ui->searchRoot->currentText();
	const QString withText = ui->fileContentsToFind->currentText();

	if (what.isEmpty() || where.isEmpty())
		return;

	_workerThread.exec([this, where, what, withText](){
		_uiQueue.enqueue([this](){
			ui->btnSearch->setText(tr("Stop"));
		});

		const auto hierarchy = flattenHierarchy(enumerateDirectoryRecursively(CFileSystemObject(where), 
			[this](QString str){
			_uiQueue.enqueue([this, str](){
				ui->progressLabel->setText(tr("Scanning...") % " " % str);
				qDebug() << (tr("Scanning...") % " " % str);
			}, 1);
		}, _workerThread.terminationFlag()));

		_uiQueue.enqueue([this](){
			ui->progressLabel->clear();
			ui->btnSearch->setText(tr("Search"));
		});
	});
}

void CFilesSearchWindow::updateUi()
{
	_uiQueue.exec();
}
