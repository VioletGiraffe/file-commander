#include "cfilessearchwindow.h"
#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "../cmainwindow.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <QDebug>
RESTORE_COMPILER_WARNINGS

#define SETTINGS_NAME_TO_FIND     "FileSearchDialog/Ui/NameToFind"
#define SETTINGS_CONTENTS_TO_FIND "FileSearchDialog/Ui/ContentsToFind"
#define SETTINGS_ROOT_FOLDER      "FileSearchDialog/Ui/RootFolder"

CFilesSearchWindow::CFilesSearchWindow(const QString& root) :
	QMainWindow(nullptr),
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

	connect(ui->nameToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->fileContentsToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);

	_engine.addListener(this);

	_progressLabel = new QLabel(this);
	assert_r(statusBar());
	statusBar()->addWidget(_progressLabel, 1);
	statusBar()->setSizePolicy(QSizePolicy::Ignored, statusBar()->sizePolicy().verticalPolicy());

	connect(ui->resultsList, &QListWidget::itemActivated, [](QListWidgetItem* item){
		CController::get().activePanel().goToItem(CFileSystemObject(item->data(Qt::UserRole).toString()));
		CMainWindow::get()->activateWindow();
	});

	QTimer::singleShot(0, [this](){
		ui->nameToFind->setFocus();
		ui->nameToFind->lineEdit()->selectAll();
	});
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	_engine.removeListener(this);
	_engine.stopSearching();
	delete ui;
}

void CFilesSearchWindow::itemScanned(const QString& currentItem)
{
	_progressLabel->setText(currentItem);
}

void CFilesSearchWindow::matchFound(const QString& path)
{
	const bool isDir = QFileInfo(path).isDir();

	QListWidgetItem* item = new QListWidgetItem;
	item->setText(isDir ? ("[" % path % "]") : path);
	item->setData(Qt::UserRole, path);
	ui->resultsList->addItem(item);
}

void CFilesSearchWindow::searchFinished(CFileSearchEngine::SearchStatus status, uint32_t speed)
{
	ui->btnSearch->setText(tr("Start"));
	QString message = (status == CFileSearchEngine::SearchCancelled ? tr("Search aborted") : tr("Search completed"));
	if (speed > 0)
		message = message % ", " % tr("search speed: %1 items/sec").arg(speed);
	_progressLabel->setText(message);
	ui->resultsList->setFocus();
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
	setWindowTitle('\"' % what % "\" " % tr("search results"));
}
