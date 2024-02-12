#include "cfilessearchwindow.h"
#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "../cmainwindow.h"
#include "settings/csettings.h"
#include "filesystemhelperfunctions.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "widgets/cpersistentwindow.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#define SETTINGS_NAME_TO_FIND            QSL("FileSearchDialog/Ui/NameToFind")
#define SETTINGS_NAME_CASE_SENSITIVE     QSL("FileSearchDialog/Ui/CaseSensitiveName")
#define SETTINGS_CONTENTS_TO_FIND        QSL("FileSearchDialog/Ui/ContentsToFind")
#define SETTINGS_CONTENTS_CASE_SENSITIVE QSL("FileSearchDialog/Ui/CaseSensitiveContents")
#define SETTINGS_ROOT_FOLDER             QSL("FileSearchDialog/Ui/RootFolder")

CFilesSearchWindow::CFilesSearchWindow(const std::vector<QString>& targets, QWidget* parent) :
	QMainWindow(parent),
	ui(new Ui::CFilesSearchWindow),
	_engine(CController::get().fileSearchEngine())
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	installEventFilter(new CPersistenceEnabler(QSL("UI/FileSearchWindow"), this));

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	ui->nameToFind->enableAutoSave(SETTINGS_NAME_TO_FIND);
	ui->fileContentsToFind->enableAutoSave(SETTINGS_CONTENTS_TO_FIND);
	ui->fileContentsToFind->setSaveCurrentText(true);
	ui->searchRoot->enableAutoSave(SETTINGS_ROOT_FOLDER);

	QString pathsToSearchIn;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		pathsToSearchIn.append(targets[i]);
		if (i < targets.size() - 1)
			pathsToSearchIn.append(QSL("; "));
	}
	ui->searchRoot->setCurrentText(toNativeSeparators(pathsToSearchIn));

	CSettings s;
	ui->cbNameCaseSensitive->setChecked(s.value(SETTINGS_NAME_CASE_SENSITIVE, false).toBool());
	ui->cbContentsCaseSensitive->setChecked(s.value(SETTINGS_CONTENTS_CASE_SENSITIVE, false).toBool());

	connect(ui->nameToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->fileContentsToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);

	connect(ui->btnSaveResults, &QPushButton::clicked, this, &CFilesSearchWindow::saveResults);
	connect(ui->btnLoadResults, &QPushButton::clicked, this, &CFilesSearchWindow::loadResults);

	_engine.addListener(this);

	_progressLabel = new QLabel(this);
	assert_r(statusBar());
	statusBar()->addWidget(_progressLabel, 1);
	statusBar()->setSizePolicy(QSizePolicy::Ignored, statusBar()->sizePolicy().verticalPolicy());

	connect(ui->resultsList, &QListWidget::itemActivated, [](QListWidgetItem* item) {
		CController::get().activePanel().goToItem(CFileSystemObject(item->data(Qt::UserRole).toString()));
		CMainWindow::get()->activateWindow();
	});

	QTimer::singleShot(0, this, [this](){
		ui->nameToFind->setFocus();
		ui->nameToFind->lineEdit()->selectAll();
	});

	ui->cbNameCaseSensitive->setVisible(caseSensitiveFilesystem());
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
	addResultToUi(path);
}

void CFilesSearchWindow::searchFinished(CFileSearchEngine::SearchStatus status, uint32_t speed)
{
	ui->btnSearch->setText(tr("Start"));
	QString message = (status == CFileSearchEngine::SearchCancelled ? tr("Search aborted") : tr("Search completed"));
	if (!_matches.empty())
		message = message % ", " % tr("%1 items found").arg(_matches.size());
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

	_matches.clear();

	const QString what = ui->nameToFind->currentText();
	const QString withText = ui->fileContentsToFind->currentText();

	if (_engine.search(
		what,
		ui->cbNameCaseSensitive->isChecked(),
		ui->searchRoot->currentText().split(QSL("; ")),
		withText,
		ui->cbContentsCaseSensitive->isChecked(),
		ui->cbContentsWholeWords->isChecked()
		)
	)
	{
		ui->btnSearch->setText(tr("Stop"));
		ui->resultsList->clear();

		QString title = what;
		if (!withText.isEmpty())
			title += '/' + withText;
		title += ' ' + tr("search results");
		setWindowTitle(title);
	}
}

void CFilesSearchWindow::addResultToUi(const QString& path)
{
	ui->resultsList->setUpdatesEnabled(false);

	const bool isDir = QFileInfo(path).isDir();

	auto* item = new QListWidgetItem;
	const QString nativePath = toNativeSeparators(path);
	item->setText(isDir ? ('[' % nativePath % ']') : nativePath);
	item->setData(Qt::UserRole, path);
	ui->resultsList->addItem(item);

	ui->resultsList->scrollToBottom();

	ui->resultsList->setUpdatesEnabled(true);
}

void CFilesSearchWindow::saveResults()
{
	if (_matches.empty())
		return;

	const QString path = QFileDialog::getSaveFileName(this, {}, ui->searchRoot->currentText().split(';').front(), QSL("*.searchresult"));
	if (path.isEmpty())
		return;

	QFile file{ path };
	if (!file.open(QFile::WriteOnly))
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to save the search result to %1").arg(path));
		return;
	}

	for (const auto& match : _matches)
	{
		file.write(match.toUtf8());
		file.write("\n", 1);
	}
}

void CFilesSearchWindow::loadResults()
{
	const QString path = QFileDialog::getOpenFileName(this, {}, ui->searchRoot->currentText().split(';').front(), QSL("*.searchresult"));
	if (path.isEmpty())
		return;

	QFile file{ path };
	if (!file.open(QFile::ReadOnly))
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to open the file %1").arg(path));
		return;
	}

	QTextStream stream{ &file };
	stream.setEncoding(QStringConverter::Utf8);

	QString line;
	while (stream.readLineInto(&line))
	{
		_matches.push_back(line);
		addResultToUi(line);
	}
}
