#include "cfilessearchwindow.h"
#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "../cmainwindow.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <QDebug>
RESTORE_COMPILER_WARNINGS

#define SETTINGS_NAME_TO_FIND            "FileSearchDialog/Ui/NameToFind"
#define SETTINGS_NAME_CASE_SENSITIVE     "FileSearchDialog/Ui/CaseSensitiveName"
#define SETTINGS_CONTENTS_TO_FIND        "FileSearchDialog/Ui/ContentsToFind"
#define SETTINGS_CONTENTS_CASE_SENSITIVE "FileSearchDialog/Ui/CaseSensitiveContents"
#define SETTINGS_ROOT_FOLDER             "FileSearchDialog/Ui/RootFolder"

static bool caseSensitiveFilesystem()
{
#if defined _WIN32
	return false;
#elif __APPLE__
	return true;
#elif __linux__
	return true;
#else
#error "Unknown operating system"
	return true;
#endif
}

CFilesSearchWindow::CFilesSearchWindow(const QString& root) :
	QMainWindow(nullptr),
	ui(new Ui::CFilesSearchWindow),
	_engine(CController::get().fileSearchEngine())
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	ui->nameToFind->enableAutoSave(SETTINGS_NAME_TO_FIND);
	ui->fileContentsToFind->enableAutoSave(SETTINGS_CONTENTS_TO_FIND);
	ui->fileContentsToFind->setSaveCurrentText(true);
	ui->searchRoot->enableAutoSave(SETTINGS_ROOT_FOLDER);

	ui->searchRoot->lineEdit()->setText(root);

	CSettings s;
	ui->cbNameCaseSensitive->setChecked(s.value(SETTINGS_NAME_CASE_SENSITIVE, false).toBool());
	ui->cbContentsCaseSensitive->setChecked(s.value(SETTINGS_CONTENTS_CASE_SENSITIVE, false).toBool());

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

	ui->cbNameCaseSensitive->setVisible(caseSensitiveFilesystem());

	connect(&_resultsListUpdateTimer, &QTimer::timeout, this, &CFilesSearchWindow::addResultsToUi);
	_resultsListUpdateTimer.start(100);
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
	_matches.push_back(path);
}

void CFilesSearchWindow::searchFinished(CFileSearchEngine::SearchStatus status, uint32_t speed)
{
	ui->btnSearch->setText(tr("Start"));
	QString message = (status == CFileSearchEngine::SearchCancelled ? tr("Search aborted") : tr("Search completed"));
	if (speed > 0)
		message = message % ", " % tr("search speed: %1 items/sec").arg(speed);
	_progressLabel->setText(message);
	ui->resultsList->setFocus();
	if (ui->resultsList->count() > 0)
		ui->resultsList->item(0)->setSelected(true);
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

	_engine.search(what, ui->cbNameCaseSensitive->isChecked(), where, withText, ui->cbContentsCaseSensitive->isChecked());
	ui->btnSearch->setText(tr("Stop"));
	ui->resultsList->clear();
	setWindowTitle('\"' % what % "\" " % tr("search results"));
}

void CFilesSearchWindow::addResultsToUi()
{
	ui->resultsList->setUpdatesEnabled(false);
	for (const QString& path: _matches)
	{
		const bool isDir = QFileInfo(path).isDir();

		QListWidgetItem* item = new QListWidgetItem;
		item->setText(isDir ? ("[" % path % "]") : path);
		item->setData(Qt::UserRole, path);
		ui->resultsList->addItem(item);
	}

	ui->resultsList->scrollToBottom();
	ui->resultsList->setUpdatesEnabled(true);
	_matches.clear();
}
